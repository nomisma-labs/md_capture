# Capture applications

No capture executables are implemented yet. Add exchange applications under
`apps/<exchange>/<app>/`, with their CMake target, README and optional `config/` directory.
Feeders collect streaming market data; symbol providers acquire exchange catalogs used by
the capture workflow. Application contracts and the first supported exchange set are tracked
in [TODO.md](../TODO.md).

Declare targets with `nom_app(... IMPORT_STD ...)`, link only the components they use, and
import their named C++ modules. `json_config` supplies `nom_jcfg_generate()` and
`nom_jcfg_test()` for generated configuration and validation where needed.

See the [project overview](../README.md) and [app recipe](../docs/kit/howto.md#an-app).
