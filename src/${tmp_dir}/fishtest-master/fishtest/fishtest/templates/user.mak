<%inherit file="base.mak"/>
<h2>User administration</h2>

<form class="form-horizontal" action="${request.url}" method="POST">
  <div class="control-group">
    <label class="control-label">Username:</label>
    <label class="control-label"><a href="/tests/user/${user['username']}">${user['username']}</a></label>
  </div>
  <div class="control-group">
    <label class="control-label">eMail:</label>
    %if profile:
      <div class="controls">
        <input name="email" type="email" value="${user['email']}" required="required"/>
      </div>
    %else:
      <label class="control-label">&nbsp;<a href="mailto:${user['email']}?Subject=Fishtest%20Account">${user['email']}</a></label>
    %endif
  </div>
  %if profile:
  <div class="control-group">
    <label class="control-label">New password:</label>
    <div class="controls">
      <input name="password" type="password"/>
    </div>
  </div>
  <div class="control-group">
    <label class="control-label">Verify password:</label>
    <div class="controls">
      <input name="password2" type="password"/>
    </div>
  </div>
  %endif
  <div class="control-group">
    <label class="control-label">Registration Time:</label>
    <label class="control-label">${user['registration_time'] if 'registration_time' in user else 'Unknown'}</label>
  </div>
  <div class="control-group">
    <label class="control-label">Machine Limit:</label>
    <label class="control-label">${limit}</label>
  </div>
  <div class="control-group">
    <label class="control-label">CPU-Hours:</label>
    <label class="control-label">${hours}</label>
  </div>
  %if not profile:
  <%
  blocked = user['blocked'] if 'blocked' in user else False
  checked = 'checked' if blocked else ''
  %>
  <div class="control-group">
    <label class="control-label">Blocked:</label>
    <label class="control-label"><input name="blocked" type="checkbox" ${checked} value="True"></label>
  </div>
  %endif
  <p>
  <div class="control-group">
    <div class="controls">
      <button type="submit" class="btn btn-primary">Submit</button>
    </div>
  </div>
  </p>
  <input type="hidden" name="user" value="${user['username']}" />
</form>
