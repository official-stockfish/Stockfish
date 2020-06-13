<%page args="runs, pages=None, show_delete=False, active=False"/>

<%namespace name="base" file="base.mak"/>

%if active:
  <script type="text/javascript" src="https://www.gstatic.com/charts/loader.js"></script>
  <script type="text/javascript">
    google.charts.load('current', {'packages':['gauge']});
    google.charts.setOnLoadCallback(drawCharts);

    function drawCharts() {
      var a = -2.94, b = 2.94;
      var options = {
        redFrom: a,
        redTo: 0,
        greenFrom: 0,
        greenTo: b,
        min: a,
        max: b,
        minorTicks: 5
      };
      
      %for run in runs:
        %if 'sprt' in run['args']:
          var data = google.visualization.arrayToDataTable([
            ['Label', 'Value'],
            ['LLR', ${run['results_info']['info'][0].split(' ')[1]}]
          ]);
          var chart = new google.visualization.Gauge(
            document.getElementById('chart_div_${str(run['_id'])}')
          );
          chart.draw(data, options);
        %endif
      %endfor
    }
  </script>
%endif

<%def name="pagination()">
  %if pages and len(pages) > 3:
    <h3>
      <span class="pagination pagination-small">
        <ul>
          %for page in pages:
            <li class="${page['state']}">
              %if page['state'] not in ['disabled', 'active']:
                <a href="${page['url']}">${page['idx']}</a>
              %else:
                <a>${page['idx']}</a>
              %endif
            </li>
          %endfor
        </ul>
      </span>
    </h3>
  %endif
</%def>

${pagination()}

<table class="table table-striped table-condensed">
  <tbody>
    %for run in runs:
      <tr>
        %if show_delete:
          <td style="width: 1%;">
            <div class="dropdown">
              <button type="submit" class="btn btn-danger btn-mini" data-toggle="dropdown">
                <i class="icon-trash icon-white"></i>
              </button>
              <div class="dropdown-menu" role="menu">
                <form action="/tests/delete" method="POST" style="display: inline;">
                  <input type="hidden" name="csrf_token"
                         value="${request.session.get_csrf_token()}" />
                  <input type="hidden" name="run-id" value="${run['_id']}">
                  <button type="submit" class="btn btn-danger btn-mini">Confirm</button>
                </form>
              </div>
            </div>
          </td>

          <td style="width: 1%;">
            %if run.get('approved', False):
              <button class="btn btn-success btn-mini">
                <i class="icon-thumbs-up"></i>
              </button>
            %else:
              <button class="btn btn-warning btn-mini">
                <i class="icon-question-sign"></i>
              </button>
            %endif
          </td>
        %endif

        <td style="width: 6%;">
          ${run['start_time'].strftime("%y-%m-%d")}
        </td>

        <td style="width: 2%;">
          <a href="/tests/user/${run['args'].get('username', '')}"
             title="${run['args'].get('username', '')}">
            ${run['args'].get('username', '')[:3]}
          </a>
        </td>

        <td style="width: 16%;">
          <a href="/tests/view/${run['_id']}">${run['args']['new_tag'][:23]}</a>
        </td>

        <td style="width: 2%;">
          <a href="${h.diff_url(run)}" target="_blank" rel="noopener">diff</a>
        </td>

        <td style="width: 1%;">
          <%include file="elo_results.mak" args="run=run, show_gauge=active" />
        </td>

        <td style="width: 11%;">
          %if 'sprt' in run['args']:
            <a href="/html/live_elo.html?${str(run['_id'])}" target="_blank">sprt</a>
          %else:
            ${run['args']['num_games']}
          %endif
          @ ${run['args']['tc']} th ${str(run['args'].get('threads',1))}
          <br>
          ${('cores: '+str(run['cores'])) if not run['finished'] and 'cores' in run else ''}
        </td>

        <td style="word-break: break-word;">
          ${run['args'].get('info', '')}
        </td>
      </tr>
    %endfor
  </tbody>
</table>

${pagination()}
