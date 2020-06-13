<%inherit file="base.mak"/>

<%namespace name="base" file="base.mak"/>

%if 'spsa' in run['args']:
  <script type="text/javascript" src="https://www.google.com/jsapi"></script>
  <script type="text/javascript" src="/js/gkr.js"></script>
  <script>
    var spsa_history_url = '${run_args[0][1]}/spsa_history';
  </script>
  <script type="text/javascript" src="/js/spsa.js"></script>
%endif

<h3>
  <span>${page_title}</span>
  <a href="${h.diff_url(run)}" target="_blank" rel="noopener">diff</a>
</h3>

<div class="row-fluid">
  <div style="display:inline-block;">
    <%include file="elo_results.mak" args="run=run" />
  </div>
</div>

<div class="row-fluid">

<div class="span8" style="min-width: 540px">
  <h4>Details</h4>

	<%! import markupsafe %>

  <table class="table table-condensed">
    %for arg in run_args:
      %if len(arg[2]) == 0:
        <tr>
          <td>${arg[0]}</td>
          %if arg[0] == 'username' and approver:
            <td><a href="/user/${arg[1]}">${arg[1]}</a></td>
          %elif arg[0] == 'spsa':
            <td>
              ${arg[1][0]}<br />
              <table>
                <thead>
                  <th>param</th>
                  <th>best</th>
                  <th>start</th>
                  <th>min</th>
                  <th>max</th>
                  <th>c</th>
                  <th>a</th>
                </thead>
                <tbody>
                  %for row in arg[1][1:]:
                    <tr class="spsa-param-row">
                      %for element in row:
                        <td>${element}</td>
                      %endfor
                    </tr>
                  %endfor
                </tbody>
              </table>
            </td>
          %elif arg[0] in ['resolved_new', 'resolved_base']:
            <td>${arg[1][:10]}</td>
          %elif arg[0] == 'rescheduled_from':
            <td><a href="/tests/view/${arg[1]}">${arg[1]}</a></td>
          %else:
            <td>${str(markupsafe.Markup(arg[1])).replace('\n', '<br>') | n}</td>
          %endif
        </tr>
      %else:
        <tr>
          <td>${arg[0]}</td>
          <td>
            <a href="${arg[2]}" target="_blank" rel="noopener">
              ${str(markupsafe.Markup(arg[1]))}
            </a>
          </td>
        </tr>
      %endif
    %endfor
    <tr>
      <td>raw statistics</td>
      <td><a href=/tests/stats/${run['_id']}>/tests/stats/${run['_id']}</a></td>
    </tr>
  </table>
</div>

<div class="span4">
  <h4>Actions</h4>
  %if not run['finished']:
    <form action="/tests/stop" method="POST" style="display: inline;">
      <input type="hidden" name="run-id" value="${run['_id']}">
      <button type="submit" class="btn btn-danger">
        Stop
      </button>
    </form>
    %if not run.get('approved', False):
      <span>
        <form action="/tests/approve" method="POST" style="display: inline;">
          <input type="hidden" name="run-id" value="${run['_id']}">
          <button type="submit" id="approve-btn"
                  class="btn ${'btn-success' if run['base_same_as_master'] else 'btn-warning'}">
            Approve
          </button>
        </form>
      </span>
    %endif
  %else:
    <form action="/tests/purge" method="POST" style="display: inline;">
      <input type="hidden" name="run-id" value="${run['_id']}">
      <button type="submit" class="btn btn-danger">
        Purge
      </button>
    </form>
  %endif
  <a href="/tests/run?id=${run['_id']}">
    <button class="btn">Reschedule</button>
  </a>

  <br/>
  <br/>

  %if run.get('base_same_as_master') is not None:
    <div id="master-diff"
        class="alert ${'alert-success' if run['base_same_as_master'] else 'alert-error'}">
      %if run['base_same_as_master']:
        Base branch same as Stockfish master
      %else:
        Base branch not same as Stockfish master
      %endif
    </div>
  %endif

  %if not run.get('base_same_as_master'):
    <a href="${h.master_diff_url(run)}" target="_blank" rel="noopener">Master diff</a>
  %endif

  <hr>

  <form class="form" action="/tests/modify" method="POST">
    <label class="control-label">Number of games:</label>
    <input type="text" name="num-games" value="${run['args']['num_games']}">

    <label class="control-label">Adjust priority (higher is more urgent):</label>
    <input type="text" name="priority" value="${run['args']['priority']}">

    <label class="control-label">Adjust throughput%:</label>
    <input type="text" name="throughput" value="${run['args'].get('throughput', 1000)}">

    <div class="control-group">
      <label class="checkbox">
        Auto-purge
        <input type="checkbox" name="auto_purge"
               ${'checked' if run['args'].get('auto_purge') else ''} />
      </label>
    </div>

    <input type="hidden" name="run" value="${run['_id']}" />
    <br />
    <button type="submit" class="btn btn-primary">Modify</button>
  </form>

  %if 'spsa' not in run['args']:
    <hr>

    <h4>Stats</h4>
    <table class="table table-condensed">
      <tr><td>chi^2</td><td>${'%.2f' % (chi2['chi2'])}</td></tr>
      <tr><td>dof</td><td>${chi2['dof']}</td></tr>
      <tr><td>p-value</td><td>${'%.2f' % (chi2['p'] * 100)}%</td></tr>
    </table>
  %endif

  <hr>

  <h4>Time</h4>
  <table class="table table-condensed">
    <tr><td>start time</td><td>${run['start_time'].strftime("%Y-%m-%d %H:%M:%S")}</td></tr>
    <tr><td>last updated</td><td>${run['last_updated'].strftime("%Y-%m-%d %H:%M:%S")}</td></tr>
  </table>
</div>

</div>

%if 'spsa' in run['args']:
  <div id="div_spsa_preload" style="background-image:url('/img/preload.gif'); width: 256px; height: 32px; display: none;">
    <div style="height: 100%; width: 100%; text-align: center; padding-top: 5px;">
    Loading graph...
    </div>
  </div>

  <div id="div_spsa_error" style="display: none; border: 1px solid red; color: red; width: 400px"></div>
  <div id="chart_toolbar" style="display: none">
    Gaussian Kernel Smoother&nbsp;&nbsp;
    <div class="btn-group">
      <button id="btn_smooth_plus" class="btn">&nbsp;&nbsp;&nbsp;+&nbsp;&nbsp;&nbsp;</button>
      <button id="btn_smooth_minus" class="btn">&nbsp;&nbsp;&nbsp;−&nbsp;&nbsp;&nbsp;</button>
    </div>

    <div class="btn-group">
      <button id="btn_view_individual" type="button" class="btn btn-default dropdown-toggle" data-toggle="dropdown">
        View Individual Parameter<span class="caret"></span>
      </button>
      <ul class="dropdown-menu" role="menu" id="dropdown_individual"></ul>
    </div>

    <button id="btn_view_all" class="btn">View All</button>
  </div>
  <div id="div_spsa_history_plot"></div>
%endif

<section id="diff-section" style="display: none">
  <h3>
    Diff
    <span id="diff-num-comments" style="display: none"></span>
    <a href="${h.diff_url(run)}" class="btn btn-link" target="_blank" rel="noopener">view on Github</a>
    <button id="diff-toggle" class="btn">Show</button>
    <a href="javascript:" id="copy-diff" class="btn btn-link" style="margin-left: 10px; display: none">
      <img src="/img/clipboard.png" width="20" height="20"/> Copy apply-diff command
    </a>
    <div class="btn btn-link copied" style="color: green; display: none">Copied command!</div>
  </h3>
  <pre id="diff-contents"><code class="diff"></code></pre>
</section>

<h3>Tasks ${totals}</h3>
<table class='table table-striped table-condensed'>
 <thead>
  <tr>
   <th>Idx</th>
   <th>Worker</th>
   <th>Info</th>
   <th>Last Updated</th>
   <th>Played</th>
   <th>Wins</th>
   <th>Losses</th>
   <th>Draws</th>
   <th>Crashes</th>
   <th>Time</th>

   %if 'spsa' not in run['args']:
    <th>Residual</th>
	 %endif
  </tr>
 </thead>
 <tbody>
  %for idx, task in enumerate(run['tasks'] + run.get('bad_tasks', [])):
  <%
    stats = task.get('stats', {})
    if 'stats' in task:
      total = str(stats['wins'] + stats['losses'] + stats['draws']).zfill(3)
    else:
      continue

    if task['active'] and task['pending']:
      active_style = 'info'
    elif task['active'] and not task['pending']:
      active_style = 'error'
    else:
      active_style = ''
  %>
  <tr class="${active_style}">
   <td><a href="/api/pgn/${'%s-%d'%(run['_id'],idx)}.pgn">${idx}</a></td>
   %if 'bad' in task:
     <td style="text-decoration:line-through; background-color:#ffebeb">
   %else:
     <td>
   %endif
   %if approver and 'worker_info' in task and 'username' in task['worker_info']:
     <a href="/user/${task['worker_info']['username']}">${task['worker_key']}</a>
   %else:
     ${task['worker_key']}
   %endif
   </td>
   <td>
   %if 'worker_info' in task:
     ${task['worker_info']['uname']}
   %else:
     Unknown worker
   %endif
   </td>
   <td>${str(task.get('last_updated', '-')).split('.')[0]}</td>
   <td>${total} / ${task['num_games']}</td>
   <td>${stats.get('wins', '-')}</td>
   <td>${stats.get('losses', '-')}</td>
   <td>${stats.get('draws', '-')}</td>
   <td>${stats.get('crashes', '-')}</td>
   <td>${stats.get('time_losses', '-')}</td>

   %if 'spsa' not in run['args']:
   <td style="background-color:${task['residual_color']}">${'%.3f' % (task['residual'])}</td>
   %endif
  </tr>
  %endfor
 </tbody>
</table>

<script type="text/javascript" src="/js/highlight.diff.min.js"></script>
<script>
  document.title = '${page_title} | Stockfish Testing';

  $(function() {
    let $copyDiffBtn = $("#copy-diff");
    if (document.queryCommandSupported && document.queryCommandSupported("copy")) {
      $copyDiffBtn.on("click", () => {
        const textarea = document.createElement("textarea");
        textarea.style.position = "fixed";
        textarea.textContent = 'curl -s ${h.diff_url(run)}.diff | git apply';
        document.body.appendChild(textarea);
        textarea.select();
        try {
          document.execCommand("copy");
          $(".copied").show();
        } catch (ex) {
          console.warn("Copy to clipboard failed.", ex);
        } finally {
          document.body.removeChild(textarea);
        }
      });
    } else {
      $copyDiffBtn = null;
    }

    // Fetch the diff and decide whether to show it on the page
    const diffApiUrl = "${h.diff_url(run)}".replace("//github.com/", "//api.github.com/repos/");
    $.ajax({
      url: diffApiUrl,
      headers: {
        Accept: "application/vnd.github.v3.diff"
      },
      success: function(response) {
        const numLines = response.split("\n").length;
        const $toggleBtn = $("#diff-toggle");
        const $diffContents = $("#diff-contents");
        const $diffText = $diffContents.find("code");
        $diffText.text(response);
        $toggleBtn.on("click", function() {
          $diffContents.toggle();
          $copyDiffBtn && $copyDiffBtn.toggle();
          if ($toggleBtn.text() === "Hide") {
            $toggleBtn.text("Show");
          } else {
            $toggleBtn.text("Hide");
          }
        });
        // Hide large diffs by default
        if (numLines < 50) {
          $diffContents.show();
          $copyDiffBtn && $copyDiffBtn.show();
          $toggleBtn.text("Hide");
        } else {
          $diffContents.hide();
          $copyDiffBtn && $copyDiffBtn.hide();
          $toggleBtn.text("Show");
        }
        $("#diff-section").show();
        hljs.highlightBlock($diffText[0]);

        // Show # of comments for this diff on Github
        $.ajax({
          url: diffApiUrl,
          success: function(response) {
            let numComments = 0;
            response.commits.forEach(function(row) {
              numComments += row.commit.comment_count;
            });
            $("#diff-num-comments").text("(" + numComments + " comments)").show();
          }
        });
      }
    });
  });
</script>
<link rel="stylesheet" href="/css/highlight.github.css">
