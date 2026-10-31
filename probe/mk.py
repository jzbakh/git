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
elif cmd == 'rewrite-keep-mtime':	# same size, other content, old mtime
	p = sys.argv[2]
	st = os.stat(p)
	with open(p, 'rb') as f:
		data = f.read()
	with open(p, 'r+b') as f:
		f.write(bytes(b ^ 1 for b in data))
	os.utime(p, ns=(st.st_atime_ns, st.st_mtime_ns))
elif cmd == 'recreate-keep-mtime':	# delete and recreate, same as above
	p = sys.argv[2]
	st = os.stat(p)
	with open(p, 'rb') as f:
		data = f.read()
	os.remove(p)
	with open(p, 'wb') as f:
		f.write(bytes(b ^ 1 for b in data))
	os.utime(p, ns=(st.st_atime_ns, st.st_mtime_ns))
elif cmd == 'attr-toggle':		# flip the archive attribute
	import ctypes
	k = ctypes.windll.kernel32
	k.GetFileAttributesW.restype = ctypes.c_uint32
	p = sys.argv[2]
	a = (k.GetFileAttributesW(p) & ~0x80) ^ 0x20
	if not k.SetFileAttributesW(p, a or 0x80):
		sys.exit('SetFileAttributesW failed')
elif cmd == 'rewrite-older-ctime':	# as rewrite-keep-mtime, and C -= 1 day
	import ctypes
	from ctypes import wintypes
	p = sys.argv[2]
	st = os.stat(p)
	with open(p, 'rb') as f:
		data = f.read()
	with open(p, 'r+b') as f:
		f.write(bytes(b ^ 1 for b in data))
	os.utime(p, ns=(st.st_atime_ns, st.st_mtime_ns))
	k = ctypes.windll.kernel32
	k.CreateFileW.restype = wintypes.HANDLE
	h = k.CreateFileW(p, 0x100, 7, None, 3, 0, None)
	if h == wintypes.HANDLE(-1).value:
		sys.exit('CreateFileW failed')
	ft = ctypes.c_ulonglong(st.st_birthtime_ns // 100 +
				116444736000000000 - 864000000000)
	ok = k.SetFileTime(wintypes.HANDLE(h), ctypes.byref(ft), None, None)
	k.CloseHandle(wintypes.HANDLE(h))
	if not ok:
		sys.exit('SetFileTime failed')
