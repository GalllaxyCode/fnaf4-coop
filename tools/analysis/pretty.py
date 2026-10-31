"""Renders a dumped event sheet as compact text: python3 -I pretty.py <events file> <out file>"""
import re, sys
names = {}
for l in open('out/objects.tsv', encoding='utf-8'):
    p = l.rstrip('\n').split('\t')
    if len(p) >= 3:
        names[p[0]] = p[2]
CMP = {0: '=', 1: '<>', 2: '<=', 3: '<', 4: '>=', 5: '>'}
ACT = {80: 'SET', 81: 'ADD', 82: 'SUB', 31: 'SETALT', 32: 'ADDALT', 33: 'SUBALT', 24: 'DESTROY', 1: 'SETPOS', 17: 'ANIM', 26: 'HIDE', 27: 'SHOW', 2: 'SETX', 3: 'SETY'}
def fmt(raw):
    m = re.match(r'  (IF|DO) (.+?)\[t(-?\d+) n(-?\d+) o(\d+)\]( NOT)? \((.*)\)$', raw)
    if not m:
        return raw
    k, n, t, num, o, neg, p = m.groups()
    num = int(num)
    pp = []
    for part in re.split(r' \| (?=p\d+:)', p):
        c = re.match(r'p(\d+):(.*)', part)
        if not c:
            continue
        code, body = c.groups()
        cm = re.search(r'Comparison=(\d)', body)
        if code == '50':
            s = 'AV' + re.search(r'Value=(-?\d+)', body).group(1)
        elif code in ('10', '14', '26', '3', '4'):
            q = re.search(r'Value=(-?\d+)', body); s = q.group(1) if q else body[:30]
        elif 'Sample' in body:
            s = 'snd:' + re.search(r'Name=(\w+)', body).group(1)
        elif 'Timer{' in body:
            s = re.search(r'Timer=(\d+)', body).group(1) + 'ms'
        elif 'Position{' in body:
            q = re.search(r'ObjectInfoParent=(\d+) X=(-?\d+) Y=(-?\d+)', body); s = f'@{names.get(q.group(1), q.group(1))}+{q.group(2)},{q.group(3)}'
        elif 'Create{' in body:
            q = re.search(r'ObjectInfo=(\d+)', body); s = 'create ' + names.get(q.group(1), '?') if q else body[:60]
        elif 'Object{' in body:
            q = re.search(r'ObjectInfo=(\d+)', body); s = 'obj:' + names.get(q.group(1), q.group(1))
        elif 'Expressions{' in body:
            toks = re.findall(r'Expression\{ObjectType=(-?\d+) Num=(-?\d+) Size=\d+ Expression=(?:ExpressionInt\{Value=(-?\d+)|ExpressionString\{Value=([^ ]*)|(\w+))?[^}]*?\}?(?: ObjectInfo=(\d+))?', body)
            out = []
            for ty, nu, iv, sv, ot, oi in toks:
                if iv: out.append(iv)
                elif sv: out.append(repr(sv))
                elif ty == '0': out.append({'0': '', '2': '+', '4': '-', '6': '*', '8': '/'}.get(nu, 'op' + nu))
                else: out.append(f'x({ty},{nu}' + (',' + names.get(oi, oi) if oi else '') + ')')
            s = (CMP.get(int(cm.group(1)), '?') + ' ' if cm and k == 'IF' else '') + ' '.join(x for x in out if x)
        else:
            s = body[:50]
        pp.append(s)
    a = ACT.get(num, f'n{num}') if k == 'DO' else {-81: 'CMP', -27: 'ALT', -4: 'OVERLAP', -2: 'ANIMOVER', -17: 'X'}.get(num, f'c{num}')
    return f'  {k}{" NOT" if neg else ""} {n}.{a} ' + ' '.join(pp)
out = []
for raw in open(sys.argv[1], encoding='utf-8'):
    raw = raw.rstrip('\n')
    out.append(raw.split()[1] if raw.startswith('==') else fmt(raw))
open(sys.argv[2], 'w').write('\n'.join(out))
