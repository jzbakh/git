import sys, time, pygit2

cmd, path = sys.argv[1], sys.argv[2]
repo = pygit2.Repository(path)
if cmd == 'version':
	print('pygit2', pygit2.__version__, 'libgit2', pygit2.LIBGIT2_VERSION)
elif cmd == 'status':
	t = time.perf_counter()
	st = repo.status()
	print(f'{len(st)} changed, {time.perf_counter() - t:.3f}s')
elif cmd == 'update-index':
	flag = getattr(pygit2, 'GIT_DIFF_UPDATE_INDEX', None)
	if flag is None:
		from pygit2.enums import DiffOption
		flag = DiffOption.UPDATE_INDEX
	repo.index.diff_to_workdir(flag)
