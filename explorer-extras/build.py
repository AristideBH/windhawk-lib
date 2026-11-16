"""Joins explorer-extras/src/*.cpp into explorer-extras/explorer-extras.wh.cpp.

Windhawk takes one file, so the source is kept in parts and joined here. Edit the
parts, run this, and never edit the generated file by hand.

Two places in the parts are filled from settings.schema.json, where every setting
is declared once (see SPEC-settings-shell.md):

    @@SETTINGS_BLOCK@@   the settings block Windhawk reads, in 00-header.cpp
    @@SETTINGS_TABLE@@   the C++ table of the settings, in 02a-settings.cpp

    python explorer-extras/build.py            write the generated file
    python explorer-extras/build.py --check    exit 1 if it is out of date
"""
import glob
import json
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))


def load_schema():
    with open(os.path.join(here, 'settings.schema.json'), encoding='utf8') as handle:
        schema = json.load(handle)
    seen = set()
    for setting in schema['settings']:
        if setting['id'] in seen:
            sys.exit('settings.schema.json: the id %s is declared twice' % setting['id'])
        seen.add(setting['id'])
        for field in ('name', 'description'):
            text = setting.get(field, '')
            # A colon followed by a space breaks Windhawk's whole settings block.
            if ': ' in text or '\t' in text or '\n' in text:
                sys.exit('settings.schema.json: %s of %s has a colon and a space, a tab or a line break'
                         % (field, setting['id']))
    return schema


def yaml_value(setting):
    value = setting['default']
    if isinstance(value, bool):
        return 'true' if value else 'false'
    return str(value)


def windhawk_block(schema):
    """The text of the settings block, grouped as the schema lists the settings."""
    lines = []
    done = set()
    settings = schema['settings']
    for setting in settings:
        group = setting.get('group')
        if group:
            # Windhawk refuses a section named twice: all its settings go together.
            if group in done:
                continue
            done.add(group)
            lines.append('- %s:' % group)
            for member in settings:
                if member.get('group') == group:
                    lines.extend(entry_lines(member, '  - ', '    '))
            lines.append('  $name: %s' % schema['groups'][group]['name'])
        else:
            lines.extend(entry_lines(setting, '- ', '  '))
    return '\n'.join(lines) + '\n'


def entry_lines(setting, first, rest):
    lines = ['%s%s: %s' % (first, setting['id'], yaml_value(setting))]
    lines.append('%s$name: %s' % (rest, setting['name']))
    if setting.get('description'):
        lines.append('%s$description: %s' % (rest, setting['description']))
    if setting.get('options'):
        lines.append('%s$options:' % rest)
        for option in setting['options']:
            lines.append('%s- %s: %s' % (rest, option['value'], option['label']))
    return lines


def cpp_string(text):
    """A wide string literal. Anything outside ASCII becomes a \\u escape."""
    out = []
    for char in text:
        code = ord(char)
        if char == '\\':
            out.append('\\\\')
        elif char == '"':
            out.append('\\"')
        elif code < 32:
            sys.exit('a control character in a setting text')
        elif code < 127:
            out.append(char)
        elif code <= 0xFFFF:
            out.append('\\u%04X' % code)
        else:
            out.append('\\U%08X' % code)
    return 'L"%s"' % ''.join(out)


def cpp_table(schema):
    rows = []
    for setting in schema['settings']:
        group = setting.get('group')
        key = '%s.%s' % (group, setting['id']) if group else setting['id']
        kind = {'bool': 'Bool', 'int': 'Int', 'enum': 'Enum', 'string': 'String'}[setting['type']]
        default = setting['default']
        default_int = int(default) if setting['type'] in ('bool', 'int') else 0
        default_text = cpp_string(default) if setting['type'] in ('enum', 'string') else 'L""'
        minimum = setting.get('min', 0)
        maximum = setting.get('max', 0)
        options = '|'.join('%s=%s' % (o['value'], o['label']) for o in setting.get('options', []))
        rows.append('    {%s, %s, SettingKind::%s, %d, %s, %d, %d,\n     %s,\n     %s,\n     %s, %s, %s, %s, %s}' % (
            cpp_string(setting['id']), cpp_string(key), kind, default_int, default_text,
            minimum, maximum, cpp_string(setting['name']), cpp_string(setting.get('description', '')),
            cpp_string(setting.get('page', '')), cpp_string(setting.get('card', '')),
            'true' if setting.get('windhawkOnly') else 'false', cpp_string(options),
            'true' if setting.get('needs') == 'rebuild' else 'false'))
    cards = ',\n'.join('    {%s, %s}' % (cpp_string(k), cpp_string(v)) for k, v in schema['cards'].items())
    news = ',\n'.join('    %s' % cpp_string(item) for item in schema.get('whatsNew', []))
    strings = ',\n'.join('    {%s, %s}' % (cpp_string(k), cpp_string(v)) for k, v in schema.get('strings', {}).items())
    fr = schema.get('fr', {})
    fr_strings = ',\n'.join('    {%s, %s}' % (cpp_string(k), cpp_string(v)) for k, v in fr.get('strings', {}).items())
    fr_cards = ',\n'.join('    {%s, %s}' % (cpp_string(k), cpp_string(v)) for k, v in fr.get('cards', {}).items())
    fr_settings = ',\n'.join('    {%s, %s, %s}' % (cpp_string(k), cpp_string(v['name']), cpp_string(v['description']))
                             for k, v in fr.get('settings', {}).items())
    return ('constexpr SettingInfo kSettings[] = {\n%s,\n};\n\n'
            'constexpr SettingCard kCards[] = {\n%s,\n};\n\n'
            'constexpr const wchar_t* kWhatsNew[] = {\n%s,\n};\n\n'
            'constexpr StringInfo kStrings[] = {\n%s,\n};\n\n'
            'constexpr StringInfo kStringsFr[] = {\n%s,\n};\n\n'
            'constexpr SettingCard kCardsFr[] = {\n%s,\n};\n\n'
            'constexpr SettingText kSettingsFr[] = {\n%s,\n};\n') % (',\n'.join(rows), cards, news, strings, fr_strings, fr_cards, fr_settings)


schema = load_schema()
parts = sorted(glob.glob(os.path.join(here, 'src', '*.cpp')))
joined = b''.join(open(path, 'rb').read() for path in parts)
for marker, text in ((b'@@SETTINGS_BLOCK@@\n', windhawk_block(schema)), (b'@@SETTINGS_TABLE@@\n', cpp_table(schema))):
    if joined.count(marker) != 1:
        sys.exit('the parts must hold %s exactly once' % marker.decode().strip())
    joined = joined.replace(marker, text.encode('utf8'))


def strip_no_winrt(data):
    """The parts keep a MOD_NO_WINRT guard for a compile check without WinRT headers.
    The catalog's validator cannot resolve it, so the generated file keeps the WinRT side only."""
    out = []
    state = 0  # 0 outside, 1 inside #ifndef MOD_NO_WINRT, 2 inside its #else
    depth = 0
    for line in data.split(b'\n'):
        text = line.strip()
        if text.startswith(b'#ifndef MOD_NO_WINRT'):
            state, depth = 1, 0
            continue
        if state:
            if text.startswith((b'#if', b'#ifdef', b'#ifndef')):
                depth += 1
            elif text.startswith(b'#endif'):
                if depth == 0:
                    state = 0
                    continue
                depth -= 1
            elif depth == 0 and text.startswith(b'#else'):
                state = 2
                continue
            if state == 2:
                continue
        out.append(line)
    return b'\n'.join(out)


joined = strip_no_winrt(joined)
target = os.path.join(here, 'explorer-extras.wh.cpp')

if '--check' in sys.argv:
    current = open(target, 'rb').read() if os.path.exists(target) else b''
    sys.exit(0 if current == joined else 1)

open(target, 'wb').write(joined)
print('wrote %s from %d parts and %d settings, %d bytes' % (target, len(parts), len(schema['settings']), len(joined)))
