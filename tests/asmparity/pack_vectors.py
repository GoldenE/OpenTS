"""Losslessly package a captured text corpus; Python is not required to build tests."""

import argparse
import gzip
import hashlib
import io
from pathlib import Path
import tarfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("archive", type=Path)
    args = parser.parse_args()
    original = args.capture.read_bytes()
    digest = hashlib.sha256(original).hexdigest()
    temporary = args.archive.with_name(args.archive.name + ".tmp")
    with temporary.open("wb") as output:
        with gzip.GzipFile(filename="", mode="wb", fileobj=output, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.USTAR_FORMAT) as archive:
                member = tarfile.TarInfo(args.capture.name)
                member.size = len(original)
                member.mode = 0o644
                archive.addfile(member, io.BytesIO(original))
    with tarfile.open(temporary, "r:gz") as archive:
        if archive.getnames() != [args.capture.name]:
            raise RuntimeError("Archive member mismatch")
        restored = archive.extractfile(args.capture.name).read()
        if restored != original or hashlib.sha256(restored).hexdigest() != digest:
            raise RuntimeError("Lossless capture verification failed")
    checksum = args.archive.with_name(args.archive.name.removesuffix(".tar.gz") + ".sha256")
    checksum.write_text(digest + "\n", encoding="ascii")
    temporary.replace(args.archive)
    print(f"{args.capture.name}: {len(original)} -> {args.archive.stat().st_size} bytes; SHA256 {digest}")


if __name__ == "__main__":
    main()
