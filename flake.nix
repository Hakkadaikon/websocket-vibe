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
        # `nix build .#echo` builds the libc-hosted echo example via just.
        # The build reuses the same recipe developers run in the devShell.
        packages.echo = pkgs.stdenv.mkDerivation {
          pname = "ws-echo";
          version = "0.1.0";
          src = ./.;
          nativeBuildInputs = [ pkgs.clang_19 pkgs.lld_19 pkgs.just ];
          buildPhase = "just example";
          installPhase = "mkdir -p $out/bin && cp build/echo $out/bin/ws-echo";
        };
        packages.default = self.packages.${system}.echo;

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
