{
  inputs = {
    avr-gcc = {
      flake = false;
      url = "github:beustens/avr-gcc_mac_toolchain";
    };
    flake-utils.url = "github:numtide/flake-utils";
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };
  outputs =
    {
      avr-gcc,
      flake-utils,
      nixpkgs,
      self,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { inherit system; };
        avr-toolchain = pkgs.stdenvNoCC.mkDerivation {
          name = "avr-toolchain";
          src = avr-gcc;
          buildPhase = ":";
          installPhase = "mv AVRToolchain $out";
          fixupPhase = ":";
        };
      in
      {
        packages.default = pkgs.stdenvNoCC.mkDerivation {
          name = "magic-shelf";
          src = ./.;
          installPhase = "cp -r . $out";
          buildInputs = [ avr-toolchain ];
        };
        apps.default = {
          type = "app";
          program = "${
            pkgs.stdenvNoCC.mkDerivation {
              name = "run";
              src = ./.;
              buildPhase = ":";
              installPhase =
                let
                  contents = ''
                    #!/usr/bin/env bash

                    set -eux

                    make -C ${self.packages.${system}.default} upload
                  '';
                in
                ''
                  mkdir -p $out/bin
                  echo '${contents}' > $out/bin/run
                  chmod +x $out/bin/run
                '';
              propagatedBuildInputs = [ avr-toolchain ];
            }
          }/bin/run";
        };
      }
    );
}
