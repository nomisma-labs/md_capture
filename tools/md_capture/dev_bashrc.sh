# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

# Keep the image's normal Bash setup, then load the checkout's config commands.
if [[ -r ~/.bashrc ]]; then
    source ~/.bashrc
fi

source /workspace/external/asio_comm/external/nom_core/external/json_config/tools/json_config/config_path.sh
