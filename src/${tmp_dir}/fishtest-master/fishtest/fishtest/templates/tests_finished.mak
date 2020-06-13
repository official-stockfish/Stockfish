<%inherit file="base.mak"/>

<h2>
  Finished Tests
  %if 'success_only' in request.url:
    - Greens
  %elif 'yellow_only' in request.url:
    - Yellows
  %elif 'ltc_only' in request.url:
    - LTC
  %endif
</h2>

<h3>Finished - ${num_finished_runs} tests</h3>
<%include file="run_table.mak" args="runs=finished_runs, pages=finished_runs_pages"/>
