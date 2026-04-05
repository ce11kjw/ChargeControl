import re
import os

version = os.environ.get('VERSION', '')
date = os.environ.get('DATE', '')
notes = os.environ.get('NOTES', '')
entry = f"## [{version}] – {date}\n\n### Changed\n{notes}\n\n---\n\n"

with open('docs/CHANGELOG.md', 'r') as f:
    content = f.read()

new_content, n = re.subn(r'(## \[)', entry + r'\1', content, count=1)
if n == 0:
    new_content = content.rstrip('\n') + '\n\n' + entry

with open('docs/CHANGELOG.md', 'w') as f:
    f.write(new_content)
