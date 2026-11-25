#! /bin/bash

KEY_FILE="privkey.pem"
CRT_FILE="cert.pem"

die() {
    echo -e "\e[0;91m$*\e[0m" 1>&2
    exit 1
}

if [ $# -lt 1 ]; then
    echo "Usage: $0 DOMAIN"
    echo
    echo "* DOMAIN: domain of the OpenWhisk cluster"
    echo
    echo "A DOMAIN can be constructed via the service nip.io from the IP address"
    echo "of the cluster: simply append \".nip.io\" to the external IP address."
    echo
    echo "The script will run openssl to generate a pair of certificate and key."
    echo "They are embedded as base64 in the file mycluster.yaml."
    exit 2
fi >&2

domain="$1"

if ! which openssl >/dev/null 2>&1; then
    echo_err "missing dependency: helm."
    exit 3
fi

openssl req \
    -subj "/C=FR/CN=$domain/O=$domain" \
    -addext "subjectAltName = DNS:$domain" \
    -x509 -nodes -days 365 -newkey rsa:2048 -keyout "$KEY_FILE" -out "$CRT_FILE" \
    >/dev/null 2>&1 ||
die "failed making signed certificate"

cat  > mycluster.yaml <<EOF
k8s:
  persistence:
    enabled: false
whisk:
  ingress:
    apiHostName: "$domain"
    apiHostPort: 443
    apiHostProto: https
    type: Standard
    domain: "$domain"
    tls:
      enabled: true
      secretenabled: true
      createsecret: true
      secretname: openwhisk-ingress-tls-secret
      secrettype: kubernetes.io/tls
      crt: >-
        $(base64 -w0 "$CRT_FILE")
      key: >-
        $(base64 -w0 "$KEY_FILE")
    annotations:
      kubernetes.io/ingress.class: nginx
      kubernetes.io/tls-acme: true
      nginx.ingress.kubernetes.io/proxy-body-size: 0
  containerPool:
    userMemory: "3584m"
  # Only includes: nodejs:14, python:3, swift:4.2 and blackboxes.
  runtimes: "runtimes-minimal-travis.json"
EOF
