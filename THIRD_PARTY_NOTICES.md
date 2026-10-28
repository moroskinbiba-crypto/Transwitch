# Third-party notices

## Mozilla CA Certificate Store

The certificate bundle at `romfs/cacert.pem` contains public root CA certificates distributed by Mozilla and packaged for use with curl. The upstream curl CA extract is published at https://curl.se/docs/caextract.html. The bundle is data, not a set of Transwitch source files; the certificates remain subject to the terms associated with their respective certificate authorities and Mozilla's CA bundle distribution.

The application uses this bundle with libcurl's peer and hostname verification enabled. Certificate verification must not be disabled as a workaround for network errors.
