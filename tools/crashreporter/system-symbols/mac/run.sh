#!/bin/sh

set -v -e -x

base="$(realpath "$(dirname "$0")")"
export PATH="$PATH:$base:${MOZ_FETCHES_DIR}/dmg"

cd /builds/worker

if test "$PROCESSED_PACKAGES_INDEX" && test "$PROCESSED_PACKAGES_PATH" && test "$TASKCLUSTER_ROOT_URL"; then
  PROCESSED_PACKAGES="$TASKCLUSTER_ROOT_URL/api/index/v1/task/$PROCESSED_PACKAGES_INDEX/artifacts/$PROCESSED_PACKAGES_PATH"
fi

if test "$PROCESSED_PACKAGES"; then
  rm -f processed-packages
  if test `curl --output /dev/null --silent --head --location "$PROCESSED_PACKAGES" -w "%{http_code}"` = 200; then
    curl -L "$PROCESSED_PACKAGES" | gzip -dc > processed-packages
  elif test -f "$PROCESSED_PACKAGES"; then
    gzip -dc "$PROCESSED_PACKAGES" > processed-packages
  fi
  if test -f processed-packages; then
    # Prevent reposado from downloading packages that have previously been
    # dumped.
    for f in $(cat processed-packages); do
      mkdir -p "$(dirname "$f")"
      touch "$f"
    done
  fi
fi

mkdir -p /opt/data-reposado/html /opt/data-reposado/metadata artifacts

# First, just fetch all the update info.
python3 /usr/local/bin/repo_sync --no-download

# Next, fetch just the update packages we're interested in.
packages=$(python3 "${base}/list-packages.py")

for package in ${packages}; do
  tmp_stderr="artifacts/tmp.repo_sync-product-id-${package}.stderr"
  final_stderr="artifacts/repo_sync-product-id-${package}.stderr"

  # repo_sync is super-chatty, let's pipe stderr to separate files
  python3 /usr/local/bin/repo_sync "--product-id=${package}" 2> "$tmp_stderr"

  # Filter out known warning lines
  grep -v "has not been downloaded" "$tmp_stderr" > "$final_stderr" || true

  # Only keep non-empty stderr files
  if [ ! -s "$final_stderr" ]; then
    rm -f "$final_stderr"
  fi

  rm -f "$tmp_stderr"

  # Stop downloading packages if we have more than 10 GiB of them to process
  download_size=$(du -B1073741824 -s /opt/data-reposado | cut -f1)
  if [ ${download_size} -gt 10 ]; then
    break
  fi
done

du -sh /opt/data-reposado

# Now scrape symbols out of anything that was downloaded.
mkdir -p symbols tmp
env TMP=tmp python3 "${base}/PackageSymbolDumper.py" --tracking-file=/builds/worker/processed-packages --dump_syms=$MOZ_FETCHES_DIR/dump_syms/dump_syms /opt/data-reposado/html/content/downloads /builds/worker/symbols

# Hand out artifacts
gzip -c processed-packages > artifacts/processed-packages.gz

cd symbols
zip -r9 /builds/worker/artifacts/target.crashreporter-symbols.zip ./* || echo "No symbols dumped"
