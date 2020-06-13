<%inherit file="base.mak"/>
<h3>Pending Users</h3>

<table class="table table-striped table-condensed">
 <thead>
  <tr>
   <th>Username</th>
   <th>Registration Time</th>
   <th>eMail</th>
  </tr>
 </thead>
 <tbody>
 %for user in users:
  <tr>
   <td style="width:15%"><a href="/user/${user['username']}">${user['username']}</a></td>
   <td style="width:15%">${user['registration_time'].strftime("%y-%m-%d %H:%M:%S") if 'registration_time' in user else 'Unknown'}</td>
   <td style="width:70%">${user['email']}</td>
  </tr>
 %endfor
 </tbody>
</table>

<h3>Idle Users</h3>

<table class="table table-striped table-condensed">
 <thead>
  <tr>
   <th>Username</th>
   <th>Registration Time</th>
   <th>eMail</th>
  </tr>
 </thead>
 <tbody>
 %for user in idle:
  <tr>
   <td style="width:15%"><a href="/user/${user['username']}">${user['username']}</a></td>
   <td style="width:15%">${user['registration_time'].strftime("%y-%m-%d %H:%M:%S") if 'registration_time' in user else 'Unknown'}</td>
   <td style="width:60%">${user['email']}</td>
  </tr>
 %endfor
 </tbody>
</table>
