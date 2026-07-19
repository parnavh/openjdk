{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        devShells.default = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            autoconf
            cmake
            pkg-config
          ];

          buildInputs = with pkgs; [
            temurin-bin-26 # jdk 26
            unzip
            zip

            alsa-lib
            cups
            fontconfig
            freetype

            libX11
            libXext
            libXi
            libXrandr
            libXrender
            libXt
            libXtst
          ];

          shellHook = ''
            export NIX_HARDENING_ENABLE="fortify stackprotector"
          '';
        };
      }
    );
}
