(function() {
var raw = [], chart_object , chart_data, data_cache = [], smoothing_factor = 0, 
smoothing_max = 20, smoothing_step = 1, spsa_params, data_count, columns = [], viewAll = false;

var chart_colors = ["#3366cc", "#dc3912", "#ff9900", "#109618", "#990099", "#0099c6", "#dd4477", "#66aa00", "#b82e2e", "#316395", "#994499", "#22aa99", "#aaaa11", "#6633cc", "#e67300", "#8b0707", "#651067", "#329262", "#5574a6", "#3b3eac", "#b77322", "#16d620", "#b91383", "#f4359e", "#9c5935", "#a9c413", "#2a778d", "#668d1c", "#bea413", "#0c5922", "#743411", "#3366cc", "#dc3912", "#ff9900", "#109618", "#990099", "#0099c6", "#dd4477", "#66aa00", "#b82e2e", "#316395", "#994499", "#22aa99", "#aaaa11", "#6633cc", "#e67300", "#8b0707", "#651067", "#329262", "#5574a6", "#3b3eac", "#b77322", "#16d620", "#b91383", "#f4359e", "#9c5935", "#a9c413", "#2a778d", "#668d1c", "#bea413", "#0c5922", "#743411"];

var chart_invisible_color = '#CCCCCC';

var chart_options = {
  'curveType': 'function',
  'chartArea': {
    'width': '800',
    'height': '450',
    'left': 40,
    'top': 20
  },
  'width': 1000,
  'height': 500,
  'legend': {
    'position': 'right'
  },
  'colors': chart_colors.slice(0),
  'seriesType': "line",
  'animation':{
      duration: 800,
      easing: 'out'
    }
};

function copy_ary(arr){
    var new_arr = arr.slice(0);
    for(var i = new_arr.length; i--;)
        if(new_arr[i] instanceof Array)
            new_arr[i] = copy_ary(new_arr[i]);
    return new_arr;
}

function smooth_data(b) {

  var dt;

  //cache data table to avoid recomputing the smoothed graph
  if (data_cache[b]) {
     dt = data_cache[b];
  }
  else {

    dt = new google.visualization.DataTable();
    dt.addColumn('number', 'Iteration');
    for (i = 0; i < spsa_params.length; i++) {
        dt.addColumn('number', spsa_params[i].name); 
    }

    var d = [];
    for (j = 0; j < spsa_params.length; j++) { 
      var g = guassian_kernel_regression(copy_ary(raw[j]),  b * smoothing_step);
      if (g[0] == 0) g[0] = g[1];
      d.push(g);
    }
    
    var googleformat = [];
    for (i = 0; i < data_count; i++) {
      var c = [i];
      for (j = 0; j < spsa_params.length; j++) {
        c.push(d[j][i]);
      }
      googleformat.push(c);
    }

    dt.addRows(googleformat);
    data_cache[b] = dt;
  }

  chart_data = dt;
  redraw(true);
}

function redraw(animate) {
  var animation_params;

  if (!animate) {
    animation_params = chart_options.animation;
    chart_options.animation = {};
  }

  var view = new google.visualization.DataView(chart_data);
  view.setColumns(columns);
  chart_object.draw(view, chart_options);

  if (!animate) chart_options.animation = animation_params;
}

function update_column_visibility(col, visibility) {
  if (!visibility) {
    columns[col] = {
      label: chart_data.getColumnLabel(col),
      type: chart_data.getColumnType(col),
      calc: function () {
        return null;
      }
    };
    chart_options.colors[col - 1] = chart_invisible_color;
  } else {
    columns[col] = col;
    chart_options.colors[col - 1] = chart_colors[(col - 1) % chart_colors.length];
  }
}

$(document).ready(function(){

  $("#div_spsa_preload").fadeIn();

  //load google library
  google.load('visualization', '1.0', {packages:['corechart'], callback: function() {

    //request data for chart
    $.getJSON(spsa_history_url, function (data) {

      spsa_params = data.params;
      var spsa_history = data.param_history;

      if (!spsa_history || spsa_history.length < 2) {
        $("#div_spsa_history_plot").html("Not enough data to generate plot.").css({'border':'1px solid #EEE', 'padding': '10px', 'width': '500px'});
        return;
      };
      
      var i,j;
      for (i = 0; i < smoothing_max; i ++) {
        data_cache.push(false);
      }

      var googleformat = [];
      data_count = spsa_history.length;
      for (j = 0; j < spsa_params.length; j++) raw.push([]);

      for (i = 0; i < spsa_history.length; i++) {
        var d = [i];
        for (j = 0; j < spsa_params.length; j++) {
          d.push(spsa_history[i][j].theta);
          raw[j].push(spsa_history[i][j].theta);
        }
        googleformat.push(d);
      }

      chart_data = new google.visualization.DataTable();

      chart_data.addColumn('number', 'Iteration');
      for (i = 0; i < spsa_params.length; i++) {
          chart_data.addColumn('number', spsa_params[i].name); 
      }
      chart_data.addRows(googleformat);

      data_cache[0] = chart_data;
      chart_object = new google.visualization.LineChart(document.getElementById('div_spsa_history_plot'));
      chart_object.draw(chart_data, chart_options);

      $("#chart_toolbar").show();

      for (i = 0; i < chart_data.getNumberOfColumns(); i++) {
        columns.push(i);
      }

      for (j = 0; j < spsa_params.length; j++) { 
        $("#dropdown_individual").append("<li><a param_id=\"" + (j+1) + "\" >" + spsa_params[j].name + "</a></li>");
      }

      $("#dropdown_individual").find('a').on('click', function() {
        var param_id = $(this).attr('param_id');

        for (i = 1; i < chart_data.getNumberOfColumns(); i++) {
            update_column_visibility(i, i == param_id);
        } 

        viewAll = false;
        redraw(false);
      });
      
      //show/hide functionality
      google.visualization.events.addListener(chart_object, 'select', function(e) {
        
        var sel = chart_object.getSelection();
        if (sel.length > 0) {
          if (sel[0].row == null) {
            var col = sel[0].column;
            update_column_visibility(col, columns[col] != col);
            redraw(false);
          }
        }
        viewAll = false;
      });

    }).fail(function(xhr, status, error) {
      var msg;
      if (xhr.status === 0) {
          msg = 'No connection. Verify Network.';
      } else if (xhr.status == 404) {
          msg = '<b>HTTP 404</b>  Requested page for chart data not found.';
      } else if (xhr.status == 500) {
          msg = '<b>HTTP 500</b> Internal Server Error. Failed to retrieve chart data.';
      } else if (exception === 'parsererror') {
          msg = 'Failed to parse JSON.';
      } else if (exception === 'timeout') {
          msg = 'Time out error.';
      } else if (exception === 'abort') {
          msg = 'Request aborted.';
      } else {
          msg = '<b>Uncaught Error</b> ' + xhr.responseText;
      }
      $("#div_spsa_error").html(msg).show();
    }).always(function() { //after request is complete
      $("#div_spsa_preload").hide();
    });

    $("#btn_smooth_plus").on('click', function() {
      smoothing_factor = Math.min(smoothing_factor + 1, smoothing_max);
      smooth_data(smoothing_factor);
    });

    $("#btn_smooth_minus").on('click', function() {
      smoothing_factor = Math.max(smoothing_factor - 1,0);
      smooth_data(smoothing_factor);
    });

    $("#btn_view_all").on('click', function() {
      if (viewAll) return;
      viewAll = true;

      for (var i = 0; i < chart_data.getNumberOfColumns(); i++) {
        update_column_visibility(i, true);
      }

      redraw(false);
    });
  }});
});
})();