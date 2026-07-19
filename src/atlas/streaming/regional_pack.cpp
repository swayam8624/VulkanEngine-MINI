#include "atlas/streaming/regional_pack.hpp"

#include "atlas/core/sha256.hpp"
#include <sqlite3.h>

#include <algorithm>
#include <memory>
#include <stdexcept>

namespace vulkax::atlas {
namespace {

using Database = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

Database openDatabase(const std::filesystem::path& path, int flags) {
  sqlite3* raw = nullptr;
  const int result = sqlite3_open_v2(path.string().c_str(), &raw, flags, nullptr);
  Database database{raw, sqlite3_close};
  if (result != SQLITE_OK) {
    const std::string message =
        raw ? sqlite3_errmsg(raw) : "unknown SQLite open failure";
    throw std::runtime_error("failed to open Atlas pack: " + message);
  }
  sqlite3_busy_timeout(database.get(), 5000);
  return database;
}

void execute(sqlite3* database, const char* sql) {
  char* error = nullptr;
  if (sqlite3_exec(database, sql, nullptr, nullptr, &error) != SQLITE_OK) {
    const std::string message = error ? error : "unknown SQLite error";
    sqlite3_free(error);
    throw std::runtime_error("Atlas pack SQL failed: " + message);
  }
}

Statement prepare(sqlite3* database, const char* sql) {
  sqlite3_stmt* raw = nullptr;
  if (sqlite3_prepare_v2(database, sql, -1, &raw, nullptr) != SQLITE_OK) {
    throw std::runtime_error(
        "Atlas pack statement failed: " +
        std::string(sqlite3_errmsg(database)));
  }
  return Statement{raw, sqlite3_finalize};
}

void requireStepDone(sqlite3* database, sqlite3_stmt* statement) {
  if (sqlite3_step(statement) != SQLITE_DONE) {
    throw std::runtime_error(
        "Atlas pack write failed: " + std::string(sqlite3_errmsg(database)));
  }
}

int bindTileKey(sqlite3_stmt* statement, const AtlasTileKey& key) {
  sqlite3_bind_int(statement, 1, static_cast<int>(key.layer));
  sqlite3_bind_int(statement, 2, static_cast<int>(key.face));
  sqlite3_bind_int(statement, 3, key.level);
  sqlite3_bind_int64(statement, 4, key.x);
  sqlite3_bind_int64(statement, 5, key.y);
  return 6;
}

}  // namespace

struct AtlasPackWriter::Impl {
  explicit Impl(const std::filesystem::path& path)
      : path{path},
        database{openDatabase(
            path,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                SQLITE_OPEN_FULLMUTEX)} {
    execute(database.get(), "PRAGMA journal_mode=WAL;");
    execute(database.get(), "PRAGMA synchronous=FULL;");
    execute(
        database.get(),
        "CREATE TABLE IF NOT EXISTS metadata("
        "key TEXT PRIMARY KEY,value TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS tiles("
        "layer INTEGER NOT NULL,face INTEGER NOT NULL,level INTEGER NOT NULL,"
        "x INTEGER NOT NULL,y INTEGER NOT NULL,data BLOB NOT NULL,"
        "etag TEXT NOT NULL DEFAULT '',sha256 TEXT NOT NULL DEFAULT '',"
        "PRIMARY KEY(layer,face,level,x,y));"
        "CREATE TABLE IF NOT EXISTS poi("
        "id TEXT PRIMARY KEY,name TEXT NOT NULL,subtitle TEXT NOT NULL DEFAULT '',"
        "category TEXT NOT NULL DEFAULT '',latitude REAL NOT NULL,"
        "longitude REAL NOT NULL,altitude REAL NOT NULL,confidence REAL NOT NULL);"
        "CREATE INDEX IF NOT EXISTS poi_name ON poi(name COLLATE NOCASE);"
        "CREATE TABLE IF NOT EXISTS assets("
        "name TEXT PRIMARY KEY,data BLOB NOT NULL,sha256 TEXT NOT NULL DEFAULT '');");
  }

  std::filesystem::path path;
  Database database;
  bool transaction = false;
};

AtlasPackWriter::AtlasPackWriter(const std::filesystem::path& path)
    : impl{new Impl{path}} {}

AtlasPackWriter::~AtlasPackWriter() {
  if (impl != nullptr) {
    const auto path = impl->path;
    if (impl->transaction) {
      sqlite3_exec(impl->database.get(), "ROLLBACK;", nullptr, nullptr, nullptr);
    }
    delete impl;
    std::filesystem::remove(path.string() + "-wal");
    std::filesystem::remove(path.string() + "-shm");
  }
}

void AtlasPackWriter::begin() {
  if (impl->transaction) return;
  execute(impl->database.get(), "BEGIN IMMEDIATE;");
  impl->transaction = true;
}

void AtlasPackWriter::commit() {
  if (!impl->transaction) return;
  execute(impl->database.get(), "COMMIT;");
  impl->transaction = false;
  execute(impl->database.get(), "PRAGMA wal_checkpoint(TRUNCATE);");
  execute(impl->database.get(), "PRAGMA journal_mode=DELETE;");
}

void AtlasPackWriter::rollback() {
  if (!impl->transaction) return;
  execute(impl->database.get(), "ROLLBACK;");
  impl->transaction = false;
}

void AtlasPackWriter::setMetadata(std::string key, std::string value) {
  auto statement = prepare(
      impl->database.get(),
      "INSERT INTO metadata(key,value) VALUES(?,?) "
      "ON CONFLICT(key) DO UPDATE SET value=excluded.value;");
  sqlite3_bind_text(statement.get(), 1, key.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement.get(), 2, value.c_str(), -1, SQLITE_TRANSIENT);
  requireStepDone(impl->database.get(), statement.get());
}

void AtlasPackWriter::putTile(
    const TileRequest& request,
    const TilePayload& payload) {
  auto statement = prepare(
      impl->database.get(),
      "INSERT INTO tiles(layer,face,level,x,y,data,etag,sha256)"
      "VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(layer,face,level,x,y) DO UPDATE SET "
      "data=excluded.data,etag=excluded.etag,sha256=excluded.sha256;");
  const int dataIndex = bindTileKey(statement.get(), request.key);
  sqlite3_bind_blob(
      statement.get(),
      dataIndex,
      payload.bytes.data(),
      static_cast<int>(payload.bytes.size()),
      SQLITE_TRANSIENT);
  sqlite3_bind_text(
      statement.get(), dataIndex + 1, payload.etag.c_str(), -1, SQLITE_TRANSIENT);
  const std::string checksum =
      payload.sha256.empty() ? request.expectedSha256 : payload.sha256;
  sqlite3_bind_text(
      statement.get(), dataIndex + 2, checksum.c_str(), -1, SQLITE_TRANSIENT);
  requireStepDone(impl->database.get(), statement.get());
}

void AtlasPackWriter::putPoi(const SearchResult& result) {
  auto statement = prepare(
      impl->database.get(),
      "INSERT INTO poi(id,name,subtitle,category,latitude,longitude,altitude,"
      "confidence) VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
      "name=excluded.name,subtitle=excluded.subtitle,category=excluded.category,"
      "latitude=excluded.latitude,longitude=excluded.longitude,"
      "altitude=excluded.altitude,confidence=excluded.confidence;");
  sqlite3_bind_text(statement.get(), 1, result.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement.get(), 2, result.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(
      statement.get(), 3, result.subtitle.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(
      statement.get(), 4, result.category.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(statement.get(), 5, result.position.latitudeDegrees);
  sqlite3_bind_double(statement.get(), 6, result.position.longitudeDegrees);
  sqlite3_bind_double(statement.get(), 7, result.position.altitudeMeters);
  sqlite3_bind_double(statement.get(), 8, result.confidence);
  requireStepDone(impl->database.get(), statement.get());
}

void AtlasPackWriter::putAsset(
    std::string name,
    std::span<const uint8_t> bytes,
    std::string sha256) {
  auto statement = prepare(
      impl->database.get(),
      "INSERT INTO assets(name,data,sha256) VALUES(?,?,?) "
      "ON CONFLICT(name) DO UPDATE SET data=excluded.data,"
      "sha256=excluded.sha256;");
  sqlite3_bind_text(statement.get(), 1, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_blob(
      statement.get(),
      2,
      bytes.data(),
      static_cast<int>(bytes.size()),
      SQLITE_TRANSIENT);
  sqlite3_bind_text(statement.get(), 3, sha256.c_str(), -1, SQLITE_TRANSIENT);
  requireStepDone(impl->database.get(), statement.get());
}

AtlasPackTileSource::AtlasPackTileSource(std::filesystem::path path)
    : path{std::move(path)} {}

std::future<TilePayload> AtlasPackTileSource::request(
    TileRequest request,
    std::shared_ptr<CancellationToken> cancellation) {
  return std::async(
      std::launch::async,
      [path = path,
       request = std::move(request),
       cancellation = std::move(cancellation)] {
        if (cancellation && cancellation->isCancelled()) {
          throw std::runtime_error("Atlas pack request cancelled");
        }
        auto database = openDatabase(
            path, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX);
        auto statement = prepare(
            database.get(),
            "SELECT data,etag,sha256 FROM tiles WHERE "
            "layer=? AND face=? AND level=? AND x=? AND y=?;");
        bindTileKey(statement.get(), request.key);
        if (sqlite3_step(statement.get()) != SQLITE_ROW) {
          throw std::runtime_error(
              "tile is unavailable in Atlas regional pack: " +
              request.key.toString());
        }
        const auto* data = static_cast<const uint8_t*>(
            sqlite3_column_blob(statement.get(), 0));
        const int byteCount = sqlite3_column_bytes(statement.get(), 0);
        const char* etag =
            reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 1));
        const char* checksum =
            reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 2));
        TilePayload result{
            request.key,
            std::vector<uint8_t>(data, data + byteCount),
            etag ? etag : "",
            checksum ? checksum : "",
            false,
            false,
        };
        if (!result.sha256.empty() &&
            result.sha256 != sha256(result.bytes)) {
          throw std::runtime_error(
              "Atlas regional pack tile checksum mismatch: " +
              request.key.toString());
        }
        if (!request.knownEtag.empty() && request.knownEtag == result.etag) {
          result.bytes.clear();
          result.notModified = true;
        }
        return result;
      });
}

std::vector<SearchResult> AtlasPackTileSource::searchOffline(
    const std::string& query,
    uint32_t limit) const {
  auto database =
      openDatabase(path, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX);
  auto statement = prepare(
      database.get(),
      "SELECT id,name,subtitle,category,latitude,longitude,altitude,confidence "
      "FROM poi WHERE name LIKE ? ESCAPE '\\' COLLATE NOCASE "
      "ORDER BY confidence DESC,name ASC LIMIT ?;");
  std::string escaped;
  escaped.reserve(query.size());
  for (char character : query) {
    if (character == '%' || character == '_' || character == '\\') {
      escaped.push_back('\\');
    }
    escaped.push_back(character);
  }
  const std::string pattern = "%" + escaped + "%";
  sqlite3_bind_text(statement.get(), 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(statement.get(), 2, static_cast<int>(limit));

  std::vector<SearchResult> results;
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    auto text = [&](int column) {
      const char* value = reinterpret_cast<const char*>(
          sqlite3_column_text(statement.get(), column));
      return std::string{value ? value : ""};
    };
    results.push_back(
        {
            text(0),
            text(1),
            text(2),
            {sqlite3_column_double(statement.get(), 4),
             sqlite3_column_double(statement.get(), 5),
             sqlite3_column_double(statement.get(), 6)},
            sqlite3_column_double(statement.get(), 7),
            text(3),
        });
  }
  return results;
}

std::optional<std::vector<uint8_t>> AtlasPackTileSource::asset(
    const std::string& name) const {
  auto database =
      openDatabase(path, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX);
  auto statement =
      prepare(database.get(), "SELECT data FROM assets WHERE name=?;");
  sqlite3_bind_text(statement.get(), 1, name.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(statement.get()) != SQLITE_ROW) return std::nullopt;
  const auto* data =
      static_cast<const uint8_t*>(sqlite3_column_blob(statement.get(), 0));
  const int size = sqlite3_column_bytes(statement.get(), 0);
  return std::vector<uint8_t>{data, data + size};
}

std::optional<std::string> AtlasPackTileSource::metadata(
    const std::string& key) const {
  auto database =
      openDatabase(path, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX);
  auto statement =
      prepare(database.get(), "SELECT value FROM metadata WHERE key=?;");
  sqlite3_bind_text(statement.get(), 1, key.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(statement.get()) != SQLITE_ROW) return std::nullopt;
  const char* value =
      reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 0));
  return std::string{value ? value : ""};
}

}  // namespace vulkax::atlas
