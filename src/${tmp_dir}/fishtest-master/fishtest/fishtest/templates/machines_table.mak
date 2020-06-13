<table class="table table-striped table-condensed"
       style="max-width: 960px;">
  <thead>
    <tr>
      <th>Machine</th>
      <th>Cores</th>
      <th>MNps</th>
      <th>System</th>
      <th>Version</th>
      <th>Running on</th>
      <th>Last updated</th>
    </tr>
  </thead>
  <tbody>
    %for machine in machines:
      <tr>
        <td>${machine['username']}</td>
        <td>
          %if 'country_code' in machine:
            <div class="flag flag-${machine['country_code'].lower()}"
                 style="display: inline-block"></div>
          %endif
          ${machine['concurrency']}
        </td>
        <td>${'%.2f' % (machine['nps'] / 1000000.0)}</td>
        <td>${machine['uname']}</td>
        <td>${machine['version']}</td>
        <td>
          <a href="/tests/view/${machine['run']['_id']}">${machine['run']['args']['new_tag']}</a>
        </td>
        <td>${machine['last_updated']}</td>
      </tr>
    %endfor
    %if len(machines) == 0:
      <td>No machines running</td>
    %endif
  </tbody>
</table>