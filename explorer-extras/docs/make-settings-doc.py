"""Writes docs/settings.md from settings.schema.json. Run from the explorer-extras folder:

    python docs/make-settings-doc.py
"""
import json
import os

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
schema = json.load(open(os.path.join(root, 'settings.schema.json'), encoding='utf-8'))
pages = {'home': 'Home', 'columns': 'Columns', 'details': 'Details pane'}

out = [
    '# Explorer Extras, all settings',
    '',
    "Settings apply at once. Open the settings window from **Extras** at the bottom of the toolbar's ... menu. "
    "The same settings are on the mod's page in Windhawk. A change made in the settings window wins until the "
    "Windhawk page is edited again, and **Reset** on the Home page goes back to the Windhawk values.",
    '',
    'This file is generated from `settings.schema.json` by `docs/make-settings-doc.py`. Do not edit the tables by hand.',
    '',
]
for page in ('home', 'columns', 'details'):
    rows = [s for s in schema['settings'] if s.get('page') == page]
    if not rows:
        continue
    out += ['## ' + pages[page], '', '| Setting | Default | What it does |', '| --- | --- | --- |']
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
    "Following the active column, dragging items out of the panel and browsing archives are always on. "
    "The settings window is in English or French, following Windows' display language; Windhawk's own page for the mod is in English."
)
open(os.path.join(here, 'settings.md'), 'w', encoding='utf-8').write('\n'.join(out) + '\n')
print('wrote docs/settings.md')
