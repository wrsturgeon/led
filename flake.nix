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
        apps =
          builtins.mapAttrs
            (_: program: {
              type = "app";
              inherit program;
            })
            {
              compile-flags = "${
                pkgs.stdenvNoCC.mkDerivation {
                  name = "run";
                  src = ./.;
                  buildPhase = ":";
                  installPhase =
                    let
                      filename = "compile_flags.txt";
                      paths = [
                        "${avr-toolchain}/lib/gcc/avr/7.3.0/include"
                        "${avr-toolchain}/lib/gcc/avr/7.3.0/include-fixed"
                        "${avr-toolchain}/avr/include"
                      ];
                      includes = [
                        "${avr-toolchain}/avr/include/avr/iom4809.h"
                      ];
                      contents = ''
                        #!/usr/bin/env bash

                        set -eu

                        echo
                        echo > ${filename}
                        ${pkgs.lib.strings.concatLines (
                          builtins.map (path: ''
                            if [ -d ${path} ]; then :; else echo '${path} not found'; echo; fi
                            ${pkgs.coreutils}/bin/echo '-isystem' >> ${filename}
                            ${pkgs.coreutils}/bin/echo '${path}' >> ${filename}
                          '') paths
                        )}
                        ${pkgs.lib.strings.concatLines (
                          builtins.map (include: ''
                            if [ -f ${include} ]; then :; else echo '${include} not found'; echo; fi
                            ${pkgs.coreutils}/bin/echo '-include' >> ${filename}
                            ${pkgs.coreutils}/bin/echo '${include}' >> ${filename}
                          '') includes
                        )}
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
              default = "${
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
