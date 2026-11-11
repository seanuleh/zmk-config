import sys, re, os
stl, root, name = sys.argv[1], sys.argv[2], sys.argv[3]
vs, idx, tris = [], {}, []
cur = []
for l in open(stl):
    l = l.strip()
    if l.startswith('vertex'):
        v = tuple(round(float(x), 5) for x in l.split()[1:])
        if v not in idx: idx[v] = len(vs); vs.append(v)
        cur.append(idx[v])
        if len(cur) == 3: tris.append(cur); cur = []
mn = [min(v[i] for v in vs) for i in range(3)]; mx = [max(v[i] for v in vs) for i in range(3)]
c = [(mn[i] + mx[i]) / 2 for i in range(3)]
vs = [(v[0]-c[0], v[1]-c[1], v[2]-c[2]) for v in vs]
zoff = (mx[2] - mn[2]) / 2
p = os.path.join(root, '3D/Objects/object_1.model'); s = open(p).read()
mesh = '<mesh><vertices>' + ''.join(f'<vertex x="{x:.5f}" y="{y:.5f}" z="{z:.5f}"/>' for x, y, z in vs) + '</vertices><triangles>' + \
       ''.join(f'<triangle v1="{a}" v2="{b}" v3="{d}"/>' for a, b, d in tris) + '</triangles></mesh>'
s = re.sub(r'<mesh>.*</mesh>', mesh, s, flags=re.S); open(p, 'w').write(s)
p = os.path.join(root, '3D/3dmodel.model'); s = open(p).read()
s = s.replace('holder_x1c', name)
s = re.sub(r'transform="1 0 0 0 1 0 0 0 1 128.0000 128 [0-9.]+"', f'transform="1 0 0 0 1 0 0 0 1 128 128 {zoff:.5f}"', s); open(p, 'w').write(s)
p = os.path.join(root, 'Metadata/model_settings.config'); s = open(p).read()
s = s.replace('value="holder"', f'value="{name}"').replace('holder.stl', f'{name}.stl').replace('holder_x1c', name); open(p, 'w').write(s)
print(len(vs), 'verts', len(tris), 'tris', 'size', [round(mx[i]-mn[i], 2) for i in range(3)])
