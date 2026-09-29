# Publishing chirky.org

Chirky builds and publishes its own static website. Slimbuck links to it; no
second repository is needed for game releases. The public site contains only
the browser build, never the dashboard, Pi control API, credentials, or director
server. The static games work without the optional local world director.

## One-time AWS setup

Local sign-in details and Windows CLI commands are recorded in
`deploy/AWS.local.md` (gitignored). The reusable AWS CLI profile is `chirky`;
refresh its session with `aws sso login --profile chirky`.

Use the same AWS account as Slimbuck. Its GitHub OIDC identity provider must
already exist at `token.actions.githubusercontent.com`; the stack reuses it.
Authenticate the AWS CLI with an administrator profile locally. Do not put
access keys in this repository or GitHub variables.

Chirky.org currently uses Cloudflare nameservers. Keep its DNS there: add the
ACM validation CNAME with proxying disabled, and an apex CNAME (`@`) to the
CloudFront domain with **DNS only** selected. Cloudflare flattens the apex CNAME.
Leave `HostedZoneId` empty for this setup; no Route 53 zone is needed.

1. Request or reuse an **issued** ACM certificate for `chirky.org` in
   **us-east-1**. Use DNS validation and retain its validation CNAME for renewal.
2. Review `web-hosting.json` and deploy it in us-east-1:

   ```sh
   aws cloudformation deploy --region us-east-1 --stack-name chirky-web \
     --template-file deploy/web-hosting.json --capabilities CAPABILITY_IAM \
     --parameter-overrides CertificateArn=YOUR_ISSUED_CERTIFICATE_ARN
   ```

   For Route 53 DNS in the same account, add `HostedZoneId=YOUR_ZONE_ID` to
   the parameter overrides to create A and AAAA aliases automatically. Check
   for existing apex records before using this option. For external DNS, point
   the apex ALIAS/ANAME/flattened CNAME to the stack's `CloudFrontDomain` output.
   Do not change the domain's mail or unrelated records.
3. Inspect the stack outputs:

   ```sh
   aws cloudformation describe-stacks --region us-east-1 --stack-name chirky-web \
     --query 'Stacks[0].Outputs' --output table
   ```

4. Set these **slimbuck/chirky** GitHub Actions repository variables:
   `AWS_ROLE_ARN` = PublishRoleArn, `AWS_S3_BUCKET` = Bucket, and
   `AWS_CLOUDFRONT_DISTRIBUTION_ID` = DistributionId.
5. Run **Publish chirky.org**. Subsequent pushes to main publish automatically.
   Until AWS_ROLE_ARN is set, the workflow skips publication.

The stack uses a private, versioned S3 bucket and CloudFront origin access
control, HTTPS, and a publishing role restricted to this repository's main
branch and these resources. Deleting the stack retains the bucket. Old object
versions expire after 30 days. The initial distribution disables edge caching
to avoid mixing modules from different builds at the same URL; optimize with
versioned release paths before enabling long-lived caches.

The GitHub role trust uses this repository's immutable OIDC subject:
`repo:slimbuck@11276292/chirky@1352099473:ref:refs/heads/main`. Preserve the
owner and repository IDs when updating the policy; the old name-only subject
does not match this repository's tokens. See [GitHub's OIDC reference](https://docs.github.com/en/actions/reference/security/oidc).

## Build, check, publish

```sh
make test
make web
node tools/publish-web.cjs --check
# Set AWS_S3_BUCKET and AWS_CLOUDFRONT_DISTRIBUTION_ID, then:
node tools/publish-web.cjs
```

On Windows use `wsl make test NODE=node.exe` and `wsl make web NODE=node.exe`.
Commit and rebuild before publishing: dirty builds are rejected. Chrome is
required for integration checks; set `CHROME` if it is not in the default path.
`AWS_PROFILE` selects a local profile; `AWS_CLI` can point to a CLI executable.

Publication validates every local file hash, exercises every game through the
launcher, and checks desktop/mobile input, audio, settings, fullscreen, and
integer rendering. It uploads manifest-owned files only, explicitly sets WASM
MIME, invalidates CloudFront, and compares published bytes with the tested build.
No arbitrary directory sync or bucket-wide deletion is performed. Uploads are
not an atomic release: an already-open browser may need to reload during a
release. S3 object versions and Git history provide recovery copies.

To verify an existing release against the local build:

```sh
node tools/publish-web.cjs --verify https://chirky.org/
```

After the new site passes verification, deploy the Slimbuck link and its
CloudFront redirect for `/apps/chirky/`, preserving query strings. The Slimbuck
CloudFront Function must be published separately from its static-site build.

AWS references: [S3 origin access](https://docs.aws.amazon.com/AmazonCloudFront/latest/DeveloperGuide/private-content-restricting-access-to-s3.html),
[certificate region](https://docs.aws.amazon.com/AmazonCloudFront/latest/DeveloperGuide/cnames-and-https-requirements.html),
[GitHub OIDC roles](https://docs.aws.amazon.com/IAM/latest/UserGuide/id_roles_create_for-idp_oidc.html).
