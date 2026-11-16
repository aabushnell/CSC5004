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
        in
        {
          default = pkgs.mkShell {
            packages =
              with pkgs;
              [
                # k8s
                minikube
                kubectl

                # gcp
                google-cloud-sdk
              ]
              ++ optionals isLinux [
                # void
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

