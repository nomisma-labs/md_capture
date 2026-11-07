# Capture applications

- [OKX Spot capture](okx/okx_md_capture/README.md): JSON-configured WebSocket capture into a
  single asynchronous MD logger stream, with local TLS integration tests.

Applications live under `apps/<exchange>/<app>/`, each with its CMake target, README and
configuration. Providers and subscribers emit raw capture events; the logger does not split
symbols or interpret market payloads. The first symbol provider is planned in [TODO.md](../TODO.md).

See the [design proposal](../docs/md_capture/design_proposal.md), [project overview](../README.md)
and [app recipe](../docs/kit/howto.md#an-app).
