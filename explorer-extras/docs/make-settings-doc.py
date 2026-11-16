"""Writes docs/settings.md from settings.schema.json. Run from the explorer-extras folder:

    python docs/make-settings-doc.py
"""
import json
import os

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
schema = json.load(open(os.path.join(root, 'settings.schema.json'), encoding='utf-8'))

out = [
    '# Explorer Extras, all settings',
    '',
    "These are the settings on the mod's page in Windhawk. They apply at once, with no restart.",
    '',
    'This file is generated from `settings.schema.json` by `docs/make-settings-doc.py`. Do not edit the tables by hand.',
    '',
]
for group, info in schema['groups'].items():
    members = [s for s in schema['settings'] if s['group'] == group]
    if not members:
        continue
    out += ['## ' + info['name'], '']
    sections = [None] + list(info.get('subs', {}).keys())
    for sub in sections:
        rows = [s for s in members if s.get('sub') == sub]
        if not rows:
            continue
        if sub:
            out += ['### ' + info['subs'][sub], '']
        out += ['| Setting | Default | What it does |', '| --- | --- | --- |']
        for s in rows:
            default = s['default']
            if s['type'] == 'bool':
                default = 'on' if default else 'off'
            elif s['type'] == 'enum':
                labels = {o['value']: o['label'] for o in s.get('options', [])}
                default = labels.get(default, default)
            description = s.get('description', '') or ''
            if s['type'] == 'int' and 'min' in s:
                description = (description + ' ' if description else '') + '(%s to %s)' % (s['min'], s['max'])
            out.append('| %s | %s | %s |' % (s['name'], default, description.replace('|', '/')))
        out.append('')
out.append(
    "Following the active column, dragging items out of the panel, browsing archives, the marks of cloud, network and "
    "device items, opening a folder held under a drag, and a click in the navigation pane making its folder the first "
    "column are always on."
)
open(os.path.join(here, 'settings.md'), 'w', encoding='utf-8').write('\n'.join(out) + '\n')
print('wrote docs/settings.md')
