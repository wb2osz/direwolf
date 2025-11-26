#
#    This file is part of Dire Wolf, an amateur radio packet TNC.
#
#    Copyright (C) 2025, Martin F N Cooper, KD6YAM
#
# Purpose: Download and unzip the latest 4.x release of Hamlib.
#

import json
import pathlib
import sys
import urllib.request
import zipfile

#
# Step 1: Obtain target directory from the command line.
#

if len(sys.argv) != 2:
    print('Usage: python hamlib_download.py <target-directory>')
    exit(1)

target_dir = pathlib.Path(sys.argv[1])

#
# Step 2: Identify the latest 4.x release.
#
# We do not want the latest release per se, since that could be a later major
# version with an incompatible API. Thus we need to identify the latest 4.x
# release. We do that by querying GitHub for data on all of the releases and
# finding the tag with the greatest 4.x value.
#
# For information on the relevant GitHub JSON API, see:
#   https://docs.github.com/en/rest/releases/releases?apiVersion=2022-11-28#list-releases-for-a-repository
#

# Download the release data

LIST_RELEASES_URL = 'https://api.github.com/repos/Hamlib/Hamlib/releases'

GITHUB_API_HEADERS = {
    'Accept': 'application/vnd.github+json',
    'X-GitHub-Api-Version': '2022-11-28'
}

req = urllib.request.Request(LIST_RELEASES_URL, headers=GITHUB_API_HEADERS)
with urllib.request.urlopen(req) as f:
    data = json.loads(f.read())

# Find the latest 4.x release, by tag

tags = [r['tag_name'] for r in data]
version = next(reversed(sorted([t for t in tags if t.startswith('4.')])))
release = [r for r in data if r['tag_name'] == version][0]

#
# Step 3: Identify the URL for the Windows zip file.
#
# Most Hamlib release files include the version number in the filename, so to
# find the URL with which to download the zip file, we need to construct the
# filename based on the version number. Then we use the filename to look up
# the corresponding asset in the downloaded JSON data, and locate what is
# known as the "browser download URL".
#

zipfile_root = f'hamlib-w64-{version}'
zipfile_name = f'{zipfile_root}.zip'
asset = [a for a in release['assets'] if a['name'] == zipfile_name][0]
download_url = asset['browser_download_url']

#
# Step 4: Download and unzip the release.
#

target_dir.mkdir(parents=True, exist_ok=True)
target_file = target_dir / zipfile_name

urllib.request.urlretrieve(download_url, target_file)

zip_file = zipfile.ZipFile(target_file)
zip_file.extractall(target_dir)

#
# Step 5: Rename the top level directory.
#
# The zip file structure has a top level directory that has the same name as
# the zip file itself, thus including the version number. We rename that
# directory to simply 'hamlib' to avoid the need for the version number to be
# known outside this script.
#

orig_dir = target_dir / zipfile_root
norm_dir = target_dir / 'hamlib'

orig_dir.rename(norm_dir)

#
# End
#

