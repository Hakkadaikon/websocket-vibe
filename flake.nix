{
  description = "ws — C23 freestanding WebSocket (RFC6455) protocol stack";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in {
        devShells.default = pkgs.mkShell {
          name = "ws-dev";
          packages = with pkgs; [
            clang_19          # C23 + freestanding
            lld_19
            llvmPackages_19.clang-tools  # clang-tidy, clang-format
            just
            lizard            # cyclomatic complexity
            python3           # E2E websocket reference client + bench harness
          ];
          shellHook = ''
            echo "ws devShell — clang $(clang --version | head -1)"
          '';
        };
      });
}
