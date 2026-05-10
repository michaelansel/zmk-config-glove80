FROM nixpkgs/nix:nixos-23.11

ENV PATH=/root/.nix-profile/bin:/usr/bin:/bin

RUN <<EOF
    set -euo pipefail
    nix-env -iA cachix -f https://cachix.org/api/v1/install
    cachix use moergo-glove80-zmk-dev
    mkdir /config
    # Mirror ZMK repository to make it easier to reference both branches and
    # tags without remote namespacing
    git clone --mirror https://github.com/moergo-sc/zmk /zmk
    GIT_DIR=/zmk git worktree add --detach /src
EOF

# Prepopulate the container's nix store with the build dependencies for the main
# branch and the most recent three tags
RUN <<EOF
    cd /src
    for tag in main $(git tag -l --sort=committerdate | tail -n 3); do
      git checkout -q --detach $tag
      nix-shell --run true -A zmk ./default.nix
    done
EOF

COPY --chmod=755 <<EOF /bin/entrypoint.sh
#!/usr/bin/env bash
    set -euo pipefail
    : "\${BRANCH:=main}"

    echo "Checking out \$BRANCH from moergo-sc/zmk" >&2
    cd /src
    git fetch origin
    git checkout -q --detach "\$BRANCH"

    echo 'Building Glove80 firmware' >&2
    cd /config
    FW='import /src/default.nix {}'
    nix-build ./config -A left   --arg firmware "\$FW" -j2 -o /tmp/left   --show-trace
    nix-build ./config -A right  --arg firmware "\$FW" -j2 -o /tmp/right  --show-trace
    nix-build ./config -A dongle --arg firmware "\$FW" -j2 -o /tmp/dongle --show-trace
    install -o "\$UID" -g "\$GID" /tmp/left/zmk.uf2   ./glove80_left.uf2
    install -o "\$UID" -g "\$GID" /tmp/right/zmk.uf2  ./glove80_right.uf2
    install -o "\$UID" -g "\$GID" /tmp/dongle/zmk.uf2 ./glove80_dongle.uf2
EOF

ENTRYPOINT ["/bin/entrypoint.sh"]

# Run build.sh to use this file
