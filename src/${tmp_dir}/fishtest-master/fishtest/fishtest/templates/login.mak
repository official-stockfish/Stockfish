<%inherit file="base.mak"/>

<h3>Stockfish testing</h3>

<div class="alert alert-info alert-block">
  <h4>Permission Required</h4>
  Creating or modifying tests requires you to be logged in.
  If you don't have an account, please
  <a href="/signup">Register</a>.
</div>

<div>
  <form class="form-horizontal" action="" method="POST">
    <legend>Login</legend>
    <div class="control-group">
      <label class="control-label">Username</label>
      <div class="controls">
        <input name="username" type="text" />
      </div>
    </div>

    <div class="control-group">
      <label class="control-label">Password</label>
      <div class="controls">
        <input name="password" type="password" />
      </div>
    </div>

    <div class="control-group">
      <label class="control-label">Stay logged in</label>
      <div class="controls">
        <input name="stay_logged_in" type="checkbox" />
      </div>
    </div>

    <div class="control-group">
      <div class="controls">
        <button type="submit" class="btn btn-primary">Login</button>
      </div>
    </div>
  </form>
</div>
