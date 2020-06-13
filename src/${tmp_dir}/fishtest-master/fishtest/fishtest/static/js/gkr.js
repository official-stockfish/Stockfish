/*

From http://www.mathworks.com/matlabcentral/fileexchange/19195-kernel-smoothing-regression
Translated from Matlab code
Original implementation by Yi Cao

*/

function median(values_) {

    var values = values_.slice(0);

    values.sort( function(a,b) {return a - b;} );

    var half = Math.floor(values.length/2);

    if(values.length % 2)
        return values[half];
    else
        return (values[half-1] + values[half]) / 2.0;
}

function guassian_kernel_regression (y_, b){

  var hx_array = [], hx, hy_array = [], hy, rx = [], rf = [], h;

  var y = y_.splice(0);
  var L = y.length;
  var x = [];
  for(var i = 1; i <= L ; i++) {
    x.push(i);
    rx.push(i);
    rf.push(0);
  }

  ////////////////////////////////////////
  //    Compute bandwith 
  ////////////////////////////////////////

  var x_median = Math.round(L/2.0);
  var y_median = median(y);
  var k = (1/0.6745) * Math.pow(4.0/3.0/L, 0.2);

  for(var i = 0; i < L ; i++) {
    hy_array.push(Math.abs(y[i] - y_median) * k);
    hx_array.push(Math.abs(x[i] - x_median) * k);
  }
  hx = median(hx_array);
  hy = median(hy_array);
  h = Math.sqrt(hx * hy);

  h = h*b;
  ////////////////////////////////////////
  //    Compute Kernel
  ////////////////////////////////////////
  var t = Math.sqrt(2*Math.PI);

  for (var i = 1; i < L ; i++) {

    var u = 0;
    var zt = 0;

    for (var j = 0; j < L ; j++) {
     
      var p =  ( i - x[j] ) / h;
      var z=  Math.exp(p * -1 * p/ 2.0)/t
      zt = zt + z;
      u = u + z * y[j];
    }
    rf[i] = u / zt;

  }
  return rf;
}