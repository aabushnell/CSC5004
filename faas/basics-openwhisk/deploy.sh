#! /bin/bash

# Deploy OpenWhisk on a Google Kubernetes Cluster.
#
# Dependencies (the scripts checks for their availability):
# * ./make_mycluster.sh: local script to make mycluster.yaml, a file that
#   describes the cluster when deploying OpenWhisk;
# * gcloud: Google Cloud Platform's CLI (configured with an account, a project
#   and a zone);
# * helm: a "package manager" for Kubernetes.

# Location of the project (and consequently of the cluster).
GCLOUD_LOCATION="europe-west9-c"

# Name of the Google Kubernetes Cluster.
GKE_CLUSTER="ow-cluster"

# Helm release of the deployment of the Nginx ingress.
INGRESS_RELEASE="ow-ingress-nginx"
# Helm release of the deployment of OpenWhisk.
OW_REL="ow"
# Namespace of Kubernetes cluster where OpenWhisk.
OW_NS="openwhisk"
# Timeout to wait for Helm chart installation.
HELM_INSTALL_TIMEOUT="10m0s"

# Directory where to clone the repository of openwhisk-deploy-kube.
OW_DEPLOY_KUBE="openwhisk-deploy-kube"
# Directory of the binaries for OpenWhisk.
OW_BIN="$OW_DEPLOY_KUBE/bin"
# OpenWhisk CLI binary.
WSK="$OW_BIN/wsk"
# Default auth token for the guest namespace.
WSK_AUTH="23bc46b1-71f6-4ed5-8c54-816aa4f8c502:123zO3xZCLrMN6v2BKK1dXYFpXlPkccOFqm12CdAsMgRU4VrNZ9lyGVCGuMDGIwP"


### DISPLAY ROUTINES
if tput colors >/dev/null 2>&1; then
    reset="\e[0m"

    normal="\e[22m"
    bold="\e[1m"

    red="\e[0;91m"
    yellow="\e[0;93m"
    blue="\e[0;94m"
fi

echo_info() {
    echo -e "$blue$*$reset"
}

echo_warn() {
    echo -e "$yellow$*$reset"
}

echo_err() {
    echo -e "$red$*$reset" 1>&2
}

die() {
    echo_err "$*"
    exit 1
}
### DISPLAY ROUTINES


if ! which ./make_mycluster.sh > /dev/null; then
    echo_err "missing dependency: ./make_mycluster.sh"
    echo_err "redownload the base package for the lab: https://www-public.imtbs-tsp.eu/~mbacou/cours/practicals/basics-openwhisk/basics-openwhisk.tgz"
    exit 3
fi

if ! which jq > /dev/null; then
    echo_err "missing dependency: jq"
    echo_err "install it using your package manager (\`sudo apt-get install jq\`?)"
    exit 3
fi


if ! which gcloud > /dev/null; then
    echo_err "missing dependency: gcloud"
    echo_err "see: https://cloud.google.com/sdk/docs/install"
    exit 3
fi

if ! which helm > /dev/null; then
    echo_err "missing dependency: helm"
    echo_err "see: https://helm.sh/docs/intro/install/"
    exit 3
fi

if ! which kubectl > /dev/null; then
    echo_err "missing dependency: kubectl"
    echo_err "see: https://helm.sh/docs/intro/install/"
    exit 3
fi

if [ "$(gcloud components list --filter gke-gcloud-auth-plugin --format 'value(state.name)' 2> /dev/null)" != "Installed" ]; then
    echo_err "missing dependency: gke-cloud-auth-plugin"
    echo_err "see: https://cloud.google.com/kubernetes-engine/docs/how-to/\
cluster-access-for-kubectl#install_plugin"
    exit 3
fi

if [ "$(gcloud services list --enabled --filter "container.googleapis.com" --format 'value(state)' 2> /dev/null)" != "ENABLED" ]; then
    echo_info "enabling Kubernetes Engine API on Google Cloud"

    gcloud services enable container.googleapis.com > /dev/null ||
        die "failed enabling Kubernetes Engine API \"container.googleapis.com\""
fi

# Create the GKE cluster.
if ! gcloud container clusters describe "$GKE_CLUSTER" \
        --location "$GCLOUD_LOCATION" \
        >/dev/null 2>&1; then
    echo_info "creating Google Kubernetes Engine cluster $GKE_CLUSTER"
    echo_warn "this can be a very long operation (> 12min)"

    # Non obvious flags are here to silence warnings and notes from Google Cloud CLI.
    # --location-policy=BALANCED: balance workload between instance groups
    # * there is only one group of 2 nodes deployed here anyway
    # --enable-private-nodes: to provision nodes with only internal IP addresses.
    # * requires --enable-ip-alias for private routing.
    # --no-enable-insecure-kubelet-readonly-port: explicitely disable kubelet's insecure read-only API on port 10255
    gcloud container clusters create "$GKE_CLUSTER" \
        --location "$GCLOUD_LOCATION" \
        --num-nodes "2" --machine-type "e2-medium" \
        --disk-type "pd-balanced" --disk-size "32" \
        --location-policy=BALANCED \
        --enable-ip-alias \
        --no-enable-insecure-kubelet-readonly-port ||
        die "failed creating GKE cluster $GKE_CLUSTER"
else
    echo_warn "using existing GKE cluster $bold$GKE_CLUSTER$normal"
fi

echo_info "getting credentials for the GKE cluster $GKE_CLUSTER"

gcloud container clusters get-credentials "$GKE_CLUSTER" \
        --location "$GCLOUD_LOCATION" ||
    die "failed getting credentials for the GKU cluster $GKE_CLUSTER"

# Add the Helm repository for the Nginx ingress.
if ! helm repo list --output json |\
    jq -r '.[].name' |\
    grep --quiet '^ingress-nginx$'
then
    echo_info "adding Helm repository: ingress-nginx"

    helm repo add ingress-nginx https://kubernetes.github.io/ingress-nginx ||
        die "failed adding Helm repository for ingress-nginx"
else
    echo_warn "using already present Helm repository ingress-nginx"
fi

helm repo update >/dev/null

# The chart in the Helm repo is obsolete, but the OpenWhisk dev team waits for
# an official release to update it. So use the master Git version.
#See: https://github.com/apache/openwhisk-deploy-kube/issues/710#issuecomment-956964788.
#helm repo add openwhisk https://openwhisk.apache.org/charts ||
    #die "failed adding Helm repository for OpenWhisk"
if [ ! -d "$OW_DEPLOY_KUBE" ]; then
    echo_info "cloning openwhisk-deploy-kube Git repository"

    git clone https://gitlab.com/Thrar/openwhisk-deploy-kube.git "$OW_DEPLOY_KUBE"
else
    echo_warn "using already cloned openwhisk-deploy-kube repository at $bold\"$OW_DEPLOY_KUBE\"$normal"
fi

# Install the Helm chart of the Nginx ingress.
if ! helm --namespace "$OW_NS" status "$INGRESS_RELEASE" >/dev/null 2>&1; then
    echo_info "installing Helm chart of Nginx ingress in namespace $bold$OW_NS$normal under release $bold$INGRESS_RELEASE$normal"

    helm install --namespace "$OW_NS" --create-namespace "$INGRESS_RELEASE" \
        ingress-nginx/ingress-nginx ||
        die "failed installing Nginx ingress; recover with $bold\`helm uninstall --namespace $OW_NS $INGRESS_RELEASE\`$normal"
else
    echo_warn "using existing Helm release $bold$INGRESS_RELEASE$normal in namespace $bold$OW_NS$normal for ingress"
fi

ingress_ip=$(kubectl --namespace "$OW_NS" get services "$INGRESS_RELEASE-controller" \
    --output jsonpath='{.status.loadBalancer.ingress[0].ip}' 2>/dev/null);
[ $? -ne 0 ] && die "failed getting ingress IP address"

if [ -z "$ingress_ip" ]; then
    echo_info "waiting for ingress deployment"

    while [ -z "$ingress_ip" ]; do
        ingress_ip=$(kubectl --namespace "$OW_NS" get services "$INGRESS_RELEASE-controller" \
            --output jsonpath='{.status.loadBalancer.ingress[0].ip}' 2>/dev/null);

        sleep 2
    done
fi

echo_info "public IP address of the OpenWhisk deployment: $bold$ingress_ip$normal"

api_hostname="$ingress_ip.nip.io"
api_hostport=443

# Label cluster nodes as invokers for OpenWhisk.
nodes=$(kubectl get nodes --output jsonpath='{.items[*].metadata.name}')
[ $? -ne 0 ] && die "failed getting node list of the cluster"

for node in $nodes; do
    if [ -z "$(kubectl get nodes --output jsonpath='{.metadata.labels.openwhisk\-role}' "$node")" ]; then
        echo_info "labeling cluster node $node with the invoker role for OpenWhisk"

        kubectl label node "$node" "openwhisk-role=invoker" ||
            die "failed labeling node $node with the invoker role"
    else
        echo_warn "using already labeled node $node"
    fi
done

# Install the Helm chart of OpenWhisk.
if ! helm --namespace "$OW_NS" status "$OW_REL" >/dev/null 2>&1; then
    echo_info "making mycluster.yaml deployment file"

    ./make_mycluster.sh "$api_hostname" ||
        die "failed making mycluster.yaml"

    echo_info "installing Helm chart of OpenWhisk in namespace $bold$OW_NS$normal under release $bold$OW_REL$normal"
    echo_warn "this will wait for the end of the installation (up to $HELM_INSTALL_TIMEOUT)"

    helm install "$OW_REL" "$OW_DEPLOY_KUBE/helm/openwhisk"\
        --namespace "$OW_NS" --create-namespace\
        --values "mycluster.yaml"\
        --wait --timeout "$HELM_INSTALL_TIMEOUT" ||
        die "failed installing OpenWhisk; recover with \`helm uninstall --namespace $OW_NS $OW_REL\`"
else
    echo_warn "using existing Helm release $bold$OW_REL$normal in namespace $bold$OW_NS$normal for OpenWhisk"
fi

install_packages_status="$(kubectl --namespace "$OW_NS" get pods --selector 'name='"$OW_REL"'-install-packages' --output jsonpath='{.items[0].status.phase}')"

if [ "$install_packages_status" != Succeeded ]; then
    echo_info "waiting for the installation of sample packages"
    echo_warn "the script will poll many times to check the deployment status"

    try_n=0
    while [ "$install_packages_status" != Succeeded ]; do
        sleep 10

        try_n=$((try_n + 1))

        echo_info "testing status of $OW_REL-install-packages ($try_n)"

        install_packages_status="$(kubectl --namespace "$OW_NS" get pods --selector 'name='"$OW_REL"'-install-packages' --output jsonpath='{.items[0].status.phase}')"
    done
else
    echo_warn "sample packages already installed"
fi

echo_info "API endpoint: $bold$api_hostname:$api_hostport$normal"

# Set up OpenWhisk CLI.
if [ ! -x "$WSK" ]; then
    echo_info "downloading OpenWhisk CLI"

    mkdir -p "$OW_BIN"
    curl -L "https://github.com/apache/openwhisk-cli/releases/download/1.2.0/OpenWhisk_CLI-1.2.0-linux-amd64.tgz" | tar -C "$OW_BIN" -xz wsk ||
        die "failed downloading wsk CLI"
else
    echo_warn "using existing OpenWhisk CLI program at $bold\"$WSK\"$normal"
fi

if [ "$("$WSK" property get --output raw --apihost)" != "$api_hostname:$api_hostport" ]; then
    echo_info "configuring API host in OpenWhisk CLI"

    "$WSK" property set --apihost "$api_hostname:$api_hostport"
else
    echo_info "API host already configured in OpenWhisk CLI"
fi

if [ "$("$WSK" property get --output raw --auth)" != "$WSK_AUTH" ]; then
    echo_info "configuring auth in OpenWhisk CLI"

    "$WSK" property set --auth "$WSK_AUTH"
else
    echo_info "auth already configured in OpenWhisk CLI"
fi

echo_info "deployment done"
echo

echo_warn "to use OpenWhisk, use its CLI program at $bold\"$WSK\"$normal with the \`-i\` flag"
echo_info "add $bold\"$(realpath "$WSK")\"$normal to your PATH to use it, and make an alias with the \`-i\` flag"

echo_warn "do not forget to destroy your cluster as soon as you are finished: $bold\`gcloud container clusters delete \"$GKE_CLUSTER\"\`$normal"
