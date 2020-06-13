%if page_idx == 0:
  <h3>
    Pending - ${len(runs['pending'])} tests
    <button id="pending-button" class="btn">
      ${'Hide' if pending_shown else 'Show'}
    </button>
  </h3>

  <div id="pending"
       style="${'' if pending_shown else 'display: none;'}">
    %if len(runs['pending']) == 0:
      No pending runs
    %else:
      <%include file="run_table.mak" args="runs=runs['pending'], show_delete=True"/>
    %endif
  </div>

  %if len(failed_runs) > 0:
    <h3>Failed</h3>
    <%include file="run_table.mak" args="runs=failed_runs, show_delete=True"/>
  %endif

  <h3>Active - ${len(runs['active'])} tests</h3>
  <%include file="run_table.mak" args="runs=runs['active']"/>
%endif

<h3>Finished - ${num_finished_runs} tests</h3>
<%include file="run_table.mak" args="runs=finished_runs, pages=finished_runs_pages"/>
