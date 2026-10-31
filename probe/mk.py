import os, sys

cmd = sys.argv[1]
if cmd == 'make':		# mk.py make DIR N: N small files
	d, n = sys.argv[2], int(sys.argv[3])
	for i in range(n):
		with open(os.path.join(d, f'f{i:04}'), 'wb') as f:
			f.write((f'file {i}\n' * (1 + i % 200)).encode())
elif cmd == 'half':		# mk.py half DIR N: change every other file
	d, n = sys.argv[2], int(sys.argv[3])
	for i in range(0, n, 2):
		with open(os.path.join(d, f'f{i:04}'), 'ab') as f:
			f.write(b'changed\n')
elif cmd == 'big':		# mk.py big PATH MiB
	with open(sys.argv[2], 'wb') as f:
		for _ in range(int(sys.argv[3])):
			f.write(os.urandom(1 << 20))
elif cmd == 'rewrite':		# rewrite files in place, same content
	for p in sys.argv[2:]:
		with open(p, 'rb') as f:
			data = f.read()
		with open(p, 'r+b') as f:
			f.write(data)
