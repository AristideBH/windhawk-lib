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
    """The text of the settings block. A group of the schema is a section of Windhawk's page, a sub-group a
    section inside it. The first settings of a group (no sub-group) come before its sub-groups, in the
    order the schema lists them."""
    lines = []
    settings = schema['settings']
    done = []
    for setting in settings:
        group = setting['group']
        if group in done:
            continue
        done.append(group)
        info = schema['groups'][group]
        lines.append('- %s:' % group)
        members = [s for s in settings if s['group'] == group]
        subs = []
        for member in members:
            sub = member.get('sub')
            if not sub:
                lines.extend(entry_lines(member, '  - ', '    '))
            elif sub not in subs:
                subs.append(sub)
                lines.append('  - %s:' % sub)
                for inner in members:
                    if inner.get('sub') == sub:
                        lines.extend(entry_lines(inner, '    - ', '      '))
                lines.append('    $name: %s' % info['subs'][sub])
        lines.append('  $name: %s' % info['name'])
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
        group = setting['group']
        sub = setting.get('sub')
        key = '.'.join([group] + ([sub] if sub else []) + [setting['id']])
        kind = {'bool': 'Bool', 'int': 'Int', 'enum': 'Enum', 'string': 'String'}[setting['type']]
        default = setting['default']
        default_int = int(default) if setting['type'] in ('bool', 'int') else 0
        default_text = cpp_string(default) if setting['type'] in ('enum', 'string') else 'L""'
        rows.append('    {%s, %s, SettingKind::%s, %d, %s, %d, %d, %s}' % (
            cpp_string(setting['id']), cpp_string(key), kind, default_int, default_text,
            setting.get('min', 0), setting.get('max', 0),
            'true' if setting.get('needs') == 'rebuild' else 'false'))
    strings = ',\n'.join('    {%s, %s}' % (cpp_string(k), cpp_string(v)) for k, v in schema.get('strings', {}).items())
    fr_strings = ',\n'.join('    {%s, %s}' % (cpp_string(k), cpp_string(v))
                            for k, v in schema.get('fr', {}).get('strings', {}).items())
    return ('constexpr SettingInfo kSettings[] = {\n%s,\n};\n\n'
            'constexpr StringInfo kStrings[] = {\n%s,\n};\n\n'
            'constexpr StringInfo kStringsFr[] = {\n%s,\n};\n') % (',\n'.join(rows), strings, fr_strings)


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


def strip_verbose(data):
    """The catalog file leaves out what exists only in a test build: every #ifdef EXTRAS_VERBOSE block (the self-tests and
    their files), keeping the #else branch when there is one."""
    out = []
    state = 0  # 0 outside, 1 inside #ifdef EXTRAS_VERBOSE (dropped), 2 inside its #else (kept)
    depth = 0
    for line in data.split(bytes([10])):
        text = line.strip()
        if state == 0:
            if text == b'#ifdef EXTRAS_VERBOSE':
                state, depth = 1, 0
                continue
            out.append(line)
            continue
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
            out.append(line)
    return bytes([10]).join(out)


def strip_log_verbose(data):
    """LogVerbose is a no-op outside a test build, so the catalog file leaves its statements out. A statement is dropped only
    when it starts a line that follows a line ending in { } ; or :, so a call that is the body of an if without braces stays."""
    out = []
    lines = data.split(bytes([10]))
    i = 0
    previous = b''
    while i < len(lines):
        text = lines[i].strip()
        if text.startswith(b'LogVerbose(') and previous[-1:] in (b'{', b'}', b';', b':'):
            depth, in_string, escape, quote, j = 0, False, False, b'', i
            done = False
            while j < len(lines) and not done:
                line = lines[j]
                k = 0
                while k < len(line):
                    ch = line[k:k + 1]
                    if in_string:
                        if escape:
                            escape = False
                        elif ch == bytes([92]):
                            escape = True
                        elif ch == quote:
                            in_string = False
                    elif ch in (b'"', b"'"):
                        in_string, quote = True, ch
                    elif ch == b'(':
                        depth += 1
                    elif ch == b')':
                        depth -= 1
                        if depth == 0:
                            done = True
                            break
                    k += 1
                j += 1
            # the statement ends with ); on its last line
            if done and lines[j - 1].rstrip().endswith(b');'):
                i = j
                continue
        out.append(lines[i])
        if text and not text.startswith(b'//'):
            previous = text
        i += 1
    return bytes([10]).join(out)


joined = strip_no_winrt(joined)
if '--debug' not in sys.argv:
    joined = strip_verbose(joined)
    joined = strip_log_verbose(joined)
target = os.path.join(here, 'explorer-extras.wh.cpp')

# A test build (build.py --debug <file>) also appends every log line to that file, so a crash can be
# read afterwards. It is written beside the mod, never to the catalog file.
if '--debug' in sys.argv:
    log_path = sys.argv[sys.argv.index('--debug') + 1].replace(chr(92), '/')
    shim = open(os.path.join(here, 'debug-shim.txt'), 'rb').read().replace(bytes([13, 10]), bytes([10]))
    shim = shim.replace(b'@@LOG_PATH@@', log_path.encode('utf8'))
    at = joined.index(b'namespace {')
    joined = joined[:at] + shim + joined[at:]
    target = os.path.join(here, 'explorer-extras.debug.wh.cpp')

if '--check' in sys.argv:
    current = open(target, 'rb').read() if os.path.exists(target) else b''
    sys.exit(0 if current == joined else 1)

open(target, 'wb').write(joined)
print('wrote %s from %d parts and %d settings, %d bytes' % (target, len(parts), len(schema['settings']), len(joined)))
