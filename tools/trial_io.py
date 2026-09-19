"""Publish completed trial directories, rolling back a failed publication."""
from pathlib import Path
import os
import shutil
import tempfile


def publish_directories(staged, destination, names):
    staged, destination = Path(staged), Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    previous = Path(tempfile.mkdtemp(prefix='.hype-previous-', dir=destination))
    moved, published = [], []
    try:
        for name in names:
            target = destination / name
            if target.exists():
                os.replace(target, previous / name)
                moved.append(name)
            os.replace(staged / name, target)
            published.append(name)
    except BaseException:
        try:
            for name in reversed(published):
                os.replace(destination / name, staged / name)
            for name in reversed(moved):
                os.replace(previous / name, destination / name)
        except OSError as rollback_error:
            # Never let temporary-directory cleanup delete the only old copy.
            raise RuntimeError(f'Could not roll back publication; originals remain in {previous}') from rollback_error
        shutil.rmtree(previous)
        raise
    shutil.rmtree(previous)
