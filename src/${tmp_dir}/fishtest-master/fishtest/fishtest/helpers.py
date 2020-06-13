def tests_repo(run):
  return run['args'].get('tests_repo', 'https://github.com/official-stockfish/Stockfish')

def master_diff_url(run):
  return "https://github.com/official-stockfish/Stockfish/compare/master...{}".format(
    run['args']['resolved_base'][:10]
  )

def diff_url(run):
  if run['args'].get('spsa'):
    return master_diff_url(run)
  else:
    return "{}/compare/{}...{}".format(
      tests_repo(run),
      run['args']['resolved_base'][:10],
      run['args']['resolved_new'][:10]
    )
