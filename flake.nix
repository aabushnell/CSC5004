{
  description = "CSC5004 dev shell";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { nixpkgs, ... }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-darwin"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          inherit (pkgs.lib) optionals optionalString;
          inherit (pkgs.stdenv.hostPlatform) isLinux isDarwin;

          gdk = pkgs.google-cloud-sdk.withExtraComponents( with pkgs.google-cloud-sdk.components; [
            gke-gcloud-auth-plugin
          ]);
        in
        {
          default = pkgs.mkShell.override {
            # pin to gcc 15 for compat
            stdenv = pkgs.gcc15Stdenv;
          } {
            packages =
              with pkgs;
              [
                # utility
                openssl
                jq

                # asm
                nasm

                # c
                clang-tools
                bear

                # k8s
                kubectl
                kubernetes-helm

                # gcp
                gdk
                # google-cloud-sdk
              ]
              ++ optionals isLinux [
                linuxHeaders
              ]
              ++ optionals isDarwin [
                # void
              ];

            shellHook = ''
              export STARSHIP_GCLOUD=1

              export KUBECONFIG="$PWD/.kube/config"
              export MINIKUBE_HOME="$PWD/.minikube"

              ${optionalString isDarwin ''
                # docker-up
              ''}
            '';
          };
        }
      );
    };
}

