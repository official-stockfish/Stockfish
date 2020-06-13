<!DOCTYPE html>
<html>
<head>
  <title>Stockfish Testing Framework</title>
  <meta name="csrf-token" content="${request.session.get_csrf_token()}" />

  <link href="https://stackpath.bootstrapcdn.com/twitter-bootstrap/2.3.2/css/bootstrap-combined.min.css"
        integrity="sha384-4FeI0trTH/PCsLWrGCD1mScoFu9Jf2NdknFdFoJhXZFwsvzZ3Bo5sAh7+zL8Xgnd"
        crossorigin="anonymous"
        rel="stylesheet">
  <link href="/css/application.css?v=2" rel="stylesheet">

  <script src="https://code.jquery.com/jquery-3.4.1.min.js"
          integrity="sha384-vk5WoKIaW/vJyUAd9n/wmopsmNhiy+L2Z+SBxGYnUkunIxVxAv/UtMOhba/xskxh"
          crossorigin="anonymous"></script>
  <script src="https://stackpath.bootstrapcdn.com/twitter-bootstrap/2.3.2/js/bootstrap.min.js"
          integrity="sha384-vOWIrgFbxIPzY09VArRHMsxned7WiY6hzIPtAIIeTFuii9y3Cr6HE6fcHXy5CFhc"
          crossorigin="anonymous"></script>

  <script src="/js/jquery.cookie.js" defer></script>
  <script src="/js/application.js?v=3" defer></script>

  <%block name="head"/>
</head>

<body>
  <div class="clearfix"
       style="width: 100px; float: left; padding: 12px 0;">
    <ul class="nav nav-list">

      <li class="nav-header">Tests</li>
      <li><a href="/tests">Overview</a></li>
      <li><a href="/tests/finished?success_only=1">Greens</a></li>
      <li><a href="/tests/finished?yellow_only=1">Yellows</a></li>
      <li><a href="/tests/finished?ltc_only=1">LTC</a></li>
      %if request.authenticated_userid:
        <li><a href="/tests/user/${request.authenticated_userid}">My tests</a></li>
      %endif
      <li><a href="/tests/run">New</a></li>

      <li class="nav-header">Misc</li>
      <li><a href="/users">Users</a></li>
      <li><a href="/users/monthly">Top Month</a></li>
      <li><a href="/actions">Actions</a></li>
      <li><a href="/html/SPRTcalculator.html?elo-0=-0.5&elo-1=1.5&draw-ratio=0.61&rms-bias=0" target="_blank">SPRT Calc</a></li>

      <li class="nav-header">Github</li>
      <li><a href="https://github.com/glinscott/fishtest" target="_blank" rel="noopener">Fishtest</a></li>
      <li><a href="https://github.com/official-stockfish/Stockfish" target="_blank" rel="noopener">Stockfish</a></li>

      <li class="nav-header">Links</li>
      <li><a href="https://github.com/glinscott/fishtest/wiki" target="_blank" rel="noopener">Wiki</a></li>
      <li><a href="https://groups.google.com/forum/?fromgroups=#!forum/fishcooking" target="_blank" rel="noopener">Forum</a></li>
      <li><a href="https://groups.google.com/forum/?fromgroups=#!forum/fishcooking_results" target="_blank" rel="noopener">History</a></li>
      <li><a href="https://hxim.github.io/Stockfish-Evaluation-Guide/" target="_blank" rel="noopener">Eval Guide</a></li>
      <li><a href="https://github.com/glinscott/fishtest/wiki/Regression-Tests" target="_blank" rel="noopener">Regression</a></li>
      <li><a href="http://abrok.eu/stockfish/" target="_blank" rel="noopener">Compiles</a></li>

      <li class="nav-header">Admin</li>
      %if request.authenticated_userid:
        <li><a href="/user">Profile</a></li>
        <li><a href="/logout" id="logout">Logout</a></li>
      %else:
        <li><a href="/signup">Register</a></li>
        <li><a href="/login">Login</a></li>
      %endif
      <li>
        %if len(request.userdb.get_pending()) > 0:
          <a href="/pending"
              style="color: red;">Pending (${len(request.userdb.get_pending())})</a>
        %else:
          <a href="/pending">Pending</a>
        %endif
      </li>
    </ul>
  </div>

  <div class="clearfix"
       style="width: calc(100% - 100px); float: left;">
    <div class="container-fluid">
      <div class="row-fluid">
        <div class="flash-message">
          %if request.session.peek_flash('error'):
            <% flash = request.session.pop_flash('error') %>
            %for message in flash:
              <div class="alert alert-error">
                <button type="button" class="close" data-dismiss="alert">&times;</button>
                ${message}
              </div>
            %endfor
          %endif
          %if request.session.peek_flash():
            <% flash = request.session.pop_flash() %>
            %for message in flash:
              <div class="alert alert-success">
                <button type="button" class="close" data-dismiss="alert">&times;</button>
                ${message}
              </div>
            %endfor
          %endif
        </div>
        <main>${self.body()}</main>
      </div>
    </div>
  </div>
</body>

<script>
  (function(i,s,o,g,r,a,m){i['GoogleAnalyticsObject']=r;i[r]=i[r]||function(){
  (i[r].q=i[r].q||[]).push(arguments)},i[r].l=1*new Date();a=s.createElement(o),
  m=s.getElementsByTagName(o)[0];a.async=1;a.src=g;m.parentNode.insertBefore(a,m)
  })(window,document,'script','//www.google-analytics.com/analytics.js','ga');

  ga('create', 'UA-41961447-1', 'stockfishchess.org');
  ga('send', 'pageview');
</script>
</html>
