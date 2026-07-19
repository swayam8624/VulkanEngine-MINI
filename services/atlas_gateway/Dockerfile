FROM python:3.12.4-slim-bookworm

WORKDIR /srv/vulkax
COPY services/atlas_gateway/server.py /srv/vulkax/server.py
COPY data/atlas/navigation_replay.json /srv/vulkax/data/navigation_replay.json

USER 65532:65532
EXPOSE 8080
ENTRYPOINT ["python", "/srv/vulkax/server.py"]
CMD ["--listen", "0.0.0.0", "--replay", "/srv/vulkax/data/navigation_replay.json"]
