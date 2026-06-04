# unrar-ps5

PS5 ELF payload that extracts a RAR archive on a jailbroken PS5 and installs the extracted app into the homebrew directory layout.

The payload is intended for archives that contain a PS5 app folder with `sce_sys/param.json`. It reads the TitleID from `param.json` and installs the extracted content as:

```text
/data/homebrew/<TitleID>-app/
```

For example:

```text
/data/homebrew/PPSA06465-app/sce_sys/param.json
```

## Behavior

On injection, the payload:

1. Creates `/data/unrar/config.ini` if it does not exist.
2. Loads the configured archive, or the first `.rar` found in `/data/unrar/`.
3. Applies optional scheduling settings.
4. Removes the previous installed `<TitleID>-app` folder when it can infer the TitleID before extraction.
5. Extracts into `/data/unrar/staging`.
6. Finds `sce_sys/param.json`, reads the TitleID, removes the final install folder, and moves the extracted app into `/data/homebrew/<TitleID>-app`.
7. Optionally deletes the RAR archive volumes after a successful extraction.
8. Writes progress and errors to notifications and `/data/unrar/unrar.log`.

A lock file at `/data/unrar/unrar.lock` prevents overlapping injections. The lock uses an OS file lock, so stale lock files from a crash should not block later runs.

## Config

Default file created at `/data/unrar/config.ini`:

```ini
filename=
rar_password=
delete_after=0
extract_location=/data/homebrew
threads=0
nice=-20
cpu_mask=0
```

Options:

| Key | Default | Description |
| --- | --- | --- |
| `filename` | empty | Archive to extract. If empty, the first `.rar` in `/data/unrar/` is used. Relative paths are resolved under `/data/unrar/`; absolute paths are used as-is. |
| `rar_password` | empty | RAR password. Empty means no password is passed to UnRAR. |
| `delete_after` | `0` | Set to `1`, `true`, `yes`, or `on` to delete archive volumes after a successful extraction. |
| `extract_location` | `/data/homebrew` | Base install directory. Final path becomes `<extract_location>/<TitleID>-app`. |
| `threads` | `0` | UnRAR thread count. `0` lets UnRAR choose its default. |
| `nice` | `-20` | Process priority from `-20` to `20`. Invalid values are ignored. |
| `cpu_mask` | `0` | Optional CPU affinity mask, for example `0xff`. `0` disables affinity pinning. Some kernels may reject affinity changes; check the log. |

For the test archive used during development:

```ini
filename=
rar_password=DLPSGAME.COM
delete_after=0
extract_location=/data/homebrew
threads=0
nice=-20
cpu_mask=0
```

## Build

Build with the Docker SDK image:

```sh
docker run --rm \
  -v "$PWD:/work" \
  -w /work \
  -e PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk \
  ps5-payload-sdk:libcxx \
  make clean all
```

The output ELF is:

```text
unrar_ps5.elf
```

There is also a convenience target:

```sh
make docker-build
```

## Deploy

Place the RAR archive and optional config at:

```text
/data/unrar/
```

Inject the ELF with an ELF loader listening on port `9021`:

```sh
nc -w 10 ps5ip 9021 < unrar_ps5.elf
```

Or use the Makefile target:

```sh
make send PS5_HOST=ps5ip PS5_PORT=9021
```

## GitHub Actions Releases

This repo builds with GitHub Actions using the Docker Hub SDK image:

```text
bizkut666/ps5-payload-sdk:libcxx
```

Every push to `master` and every pull request builds `unrar_ps5.elf` and uploads it as a workflow artifact. To create a GitHub Release with the ELF attached, push a version tag:

```sh
git tag v1.0.0
git push origin v1.0.0
```

## Notifications And Logs

The payload sends PS5 notifications for:

- Extraction start.
- Progress every 10%.
- Successful install with TitleID and final path.
- Errors with a short reason.

Detailed logs are appended to:

```text
/data/unrar/unrar.log
```

Useful log fields include selected archive, extract location, delete-after state, thread count, priority result, CPU mask result, extraction time, normalization time, TitleID, and final path.

## Performance Notes

Development testing found the fastest stable defaults were:

```ini
threads=0
nice=-20
cpu_mask=0
```

`threads=0` lets UnRAR choose its internal threading. Explicit values from `1` to `8` were benchmarked and were slower in the tested archive. `nice=-20` was accepted on the tested PS5 and improved runtime. CPU affinity can be enabled with `cpu_mask`, but it may be rejected depending on kernel support; use `/data/unrar/unrar.log` to confirm.

## Troubleshooting

- `no .rar file found`: upload a `.rar` archive to `/data/unrar/` or set `filename=`.
- `bad password` or `checksum or password error`: set `rar_password=` correctly.
- `sce_sys/param.json not found after extraction`: the archive does not contain the expected PS5 app folder structure.
- `TitleID not found`: `param.json` exists but does not contain a supported TitleID key.
- `extraction already running`: another payload instance is active.
- `cpu_mask ... result=fail`: CPU affinity was rejected; leave `cpu_mask=0`.

## Release Checklist

```sh
docker run --rm \
  -v "$PWD:/work" \
  -w /work \
  -e PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk \
  ps5-payload-sdk:libcxx \
  make clean all
```

Then inject `unrar_ps5.elf`, confirm PS5 notifications, and inspect `/data/unrar/unrar.log`.
