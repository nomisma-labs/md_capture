# Capture applications

- [OKX Spot capture](okx/okx_md_capture/README.md): JSON-configured WebSocket capture into a
  single asynchronous MD logger stream, with local TLS integration tests.

Applications live under `apps/<exchange>/<app>/`, each with its CMake target, README and
configuration. Providers and subscribers emit raw capture events; the logger does not split
symbols or interpret market payloads. The first symbol provider is planned in [TODO.md](../TODO.md).
Expected coverage is around 20 data sources, with further extension possible; the list above
shows the current capture implementation.

See the [MD capture reference design](../docs/md_capture/md_capture_reference_design.md), [project overview](../README.md)
and [app recipe](../docs/kit/howto.md#an-app).
