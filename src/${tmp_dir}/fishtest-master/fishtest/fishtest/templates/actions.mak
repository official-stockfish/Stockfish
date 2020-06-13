<%inherit file="base.mak"/>
<h3> </h3>

<form>
Show only:
<select id="restrict" name="action">
  <option value="">All</option>
  <option value="new_run">New Run</option>
  <option value="approve_run">Approve Run</option>
  <option value="modify_run">Modify Run</option>
  <option value="stop_run">Stop Run</option>
  <option value="delete_run">Delete Run</option>
  <option value="purge_run">Purge Run</option>
  <option value="block_user">Block/Unblock User</option>
  <option value="update_stats">System Events</option>
</select>
&nbsp;From user:
<input id="user" type="text" name="user" class="submit_on_enter">
<br/>
<button type="submit" class="btn btn-success">Select</button>
</form>

<table class="table table-striped table-condensed">
 <thead>
  <tr>
   <th>Time</th>
   <th>Username</th>
   <th>Run/User</th>
   <th>Action</th>
  </tr>
 </thead>
 <tbody>
 %for action in actions:
  <tr>
   <td>${action['time'].strftime("%y-%m-%d %H:%M:%S")}</td>
   %if approver and 'fishtest.' not in action['username']:
   <td><a href="/user/${action['username']}">${action['username']}</a></td>
   %else:
   <td>${action['username']}</td>
   %endif
   %if 'run' in action:
   <td><a href="/tests/view/${action['_id']}">${action['run'][:23]}</a></td>
   %elif approver:
   <td><a href="/user/${action['user']}">${action['user']}</a></td>
   %else:
   <td>${action['user']}</td>
   %endif
   <td>${action['description']}</td>
  </tr>
 %endfor
 </tbody>
</table>
