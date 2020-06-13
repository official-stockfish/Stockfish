<%inherit file="base.mak"/>

<link href="/css/flags.css" rel="stylesheet">

<h2>Stockfish Testing Queue</h2>

%if page_idx == 0:
  <h3>
    <span>
      ${len(machines)} machines ${cores}
      cores ${'%.2fM' % (nps / (cores * 1000000.0 + 1))} nps
      (${'%.2fM' % (nps / (1000000.0 + 1))} total nps)
      ${games_per_minute} games/minute
      ${pending_hours} hours remaining
    </span>
    <button id="machines-button" class="btn">
      ${'Hide' if machines_shown else 'Show'}
    </button>
  </h3>

  <div id="machines"
        style="${'' if machines_shown else 'display: none;'}">
    %if machines_shown:
      <%include file="machines_table.mak" args="machines=machines"/>
    %endif
  </div>
%endif

<%include file="run_tables.mak"/>
