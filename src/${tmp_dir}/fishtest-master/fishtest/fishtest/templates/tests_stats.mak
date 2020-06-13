<%
   import copy
   import fishtest.stats.stat_util
   import fishtest.stats.LLRcalc

   has_sprt        = 'sprt'        in run['args'].keys()
   has_pentanomial = 'pentanomial' in run['results'].keys()
   has_spsa        = 'spsa'        in run['args'].keys()

   def pdf_to_string(pdf,decimals=(2,5)):
      format="%."+str(decimals[0])+"f"+": "+"%."+str(decimals[1])+"f"
      return "{"+", ".join([(format % (prob,value)) for prob,value in pdf])+"}"

   def sensitivity_conf(avg,var,skewness,exkurt):
      sensitivity=(avg-0.5)/var**.5
      norm_var_sensitivity=1-sensitivity*skewness+0.25*sensitivity**2*(exkurt+2)
      if norm_var_sensitivity<0: # in principle this cannot happen except (perhaps)
                                 # for rounding errors
      	 norm_var_sensitivity=0  
      return sensitivity,norm_var_sensitivity

   z975=fishtest.stats.stat_util.Phi_inv(0.975)
%>
<!DOCTYPE html>
<html lang="en-us">
  <head>
    <title>Raw statistics for ${run['_id']}</title>
	<link href="https://stackpath.bootstrapcdn.com/twitter-bootstrap/2.3.2/css/bootstrap-combined.min.css"
	      integrity="sha384-4FeI0trTH/PCsLWrGCD1mScoFu9Jf2NdknFdFoJhXZFwsvzZ3Bo5sAh7+zL8Xgnd"
	      crossorigin="anonymous"
	      rel="stylesheet">
    <style>
      td {
        width: 20%;
      }
    </style>
  </head>
  <body>
% if not has_spsa:
    <div class="row-fluid">
      <div class="span2">
      </div>
      <div class="span8">
      <H3> Raw statistics for ${run['_id']}</H3>
      <em> Unless otherwise specified, all Elo quantities below are logistic. </em>
      <H4> Context </H4>
      	   <table class="table table-condensed">
	   	  <tr><td>TC</td><td>${run['args']['tc']}</td></tr>
		  <tr><td>Book</td><td>${run['args']['book']}</td></tr>
		  <tr><td>Threads</td><td>${run['args']['threads']}</td></tr>
		  <tr><td>Base options</td><td>${run['args']['base_options']}</td></tr>
		  <tr><td>New options</td><td>${run['args']['new_options']}</td></tr>
	</table>
% if has_sprt:
      <H4> SPRT parameters</H4>
      <%
      alpha=run['args']['sprt']['alpha']
      beta=run['args']['sprt']['beta']
      elo0=run['args']['sprt']['elo0']
      elo1=run['args']['sprt']['elo1']
      elo_model=run['args']['sprt'].get('elo_model','BayesElo')
      o=run['args']['sprt'].get('overshoot',None)
      %>
      <table class="table table-condensed">
	<tr><td>Alpha</td><td>${alpha}</td></tr>
	<tr><td>Beta</td><td>${beta}</td></tr>
        <tr><td>Elo0 (${elo_model})</td><td>${elo0}</td></tr>
	<tr><td>Elo1 (${elo_model})</td><td>${elo1}</td></tr>
      </table>
% endif  ## has_sprt
      <H4> Logistic/BayesElo</H4>
      <%
       	 results3=[run['results']['losses'],run['results']['draws'],run['results']['wins']]
	 results3_=fishtest.stats.LLRcalc.regularize(results3)
	 drawelo=fishtest.stats.stat_util.draw_elo_calc(results3_)
	 draw_ratio=results3_[1]/float(sum(results3_))
	 if has_sprt:
	    if elo_model=='BayesElo':
	    	 lelo0,lelo1=[fishtest.stats.stat_util.bayeselo_to_elo(elo_, drawelo) for elo_ in (elo0,elo1)]
		 belo0,belo1=elo0,elo1
	    else:
	         lelo0,lelo1=elo0,elo1
		 belo0,belo1=[fishtest.stats.stat_util.elo_to_bayeselo(elo_, draw_ratio)[0] for elo_ in [elo0,elo1]]
	    score0,score1=[fishtest.stats.stat_util.L(elo_) for elo_ in (lelo0,lelo1)]
      %>
      <table class="table table-condensed">	
	<tr><td>Draw ratio</td><td>${"%.5f"%draw_ratio}</td></tr>
	<tr><td>DrawElo (BayesElo)</td><td>${"%.2f"%drawelo}</td></tr>
% if has_sprt:
	<tr><td>Elo0 (logistic)</td><td>${"%.3f"%lelo0}</td></tr>
	<tr><td>Elo0 (BayesElo)</td><td>${"%.3f"%belo0}</td></tr>
	<tr><td>Elo1 (logistic)</td><td>${"%.3f"%lelo1}</td></tr>
	<tr><td>Elo1 (BayesElo)</td><td>${"%.3f"%belo1}</td></tr>
	<tr><td>Score0</td><td>${"%.5f"%score0}</td></tr>
	<tr><td>Score1</td><td>${"%.5f"%score1}</td></tr>
% endif  ## has_sprt
      </table>
% if has_pentanomial:
      <%
	 results5=run['results']['pentanomial']
	 results5_=fishtest.stats.LLRcalc.regularize(results5)
	 N5,pdf5=fishtest.stats.LLRcalc.results_to_pdf(results5)
	 pdf5_s=pdf_to_string(pdf5)
	 games5=2*N5
         avg5,var5,skewness5,exkurt5=fishtest.stats.LLRcalc.stats_ex(pdf5)
	 avg5_l=avg5-z975*(var5/N5)**.5
	 avg5_u=avg5+z975*(var5/N5)**.5
	 var5_per_game=2*var5
	 var5_per_game_l=var5_per_game*(1-z975*((exkurt5+2)/N5)**.5)
	 var5_per_game_u=var5_per_game*(1+z975*((exkurt5+2)/N5)**.5)
	 stdev5_per_game=var5_per_game**.5
	 stdev5_per_game_l=var5_per_game_l**.5 if var5_per_game_l>=0 else 0.0
	 stdev5_per_game_u=var5_per_game_u**.5
	 sens5,norm_var_sens5=sensitivity_conf(avg5,var5,skewness5,exkurt5)
	 sens5_l=sens5-z975*(norm_var_sens5/N5)**.5
	 sens5_u=sens5+z975*(norm_var_sens5/N5)**.5
	 sqrt2=2**.5
	 sens5_per_game=sens5/sqrt2
	 sens5_per_game_l=sens5_l/sqrt2
	 sens5_per_game_u=sens5_u/sqrt2
	 results5_DD_prob=draw_ratio-(results5_[1]+results5_[3])/(2*float(N5))
	 results5_WL_prob=results5_[2]/float(N5)-results5_DD_prob
	 if has_sprt:
	    sp=fishtest.stats.sprt.sprt(alpha=alpha,beta=beta,elo0=lelo0,elo1=lelo1)
	    sp.set_state(results5_)
	    a5=sp.analytics()
	    LLR5_l=a5['a']
	    LLR5_u=a5['b']
	    LLR5=fishtest.stats.LLRcalc.LLR_logistic(lelo0,lelo1,results5_)
	    o0=0
	    o1=0
	    if o!=None:
	       o0=-o['sq0']/o['m0']/2 if o['m0']!=0 else 0
	       o1=o['sq1']/o['m1']/2 if o['m1']!=0 else 0
	    elo5_l=a5['ci'][0]
	    elo5_u=a5['ci'][1]
	    elo5=a5['elo']
	    LOS5=a5['LOS']
	    # auxilliary
	    LLR5_exact=N5*fishtest.stats.LLRcalc.LLR(pdf5,score0,score1)
	    LLR5_alt  =N5*fishtest.stats.LLRcalc.LLR_alt(pdf5,score0,score1)
	    LLR5_alt2 =N5*fishtest.stats.LLRcalc.LLR_alt2(pdf5,score0,score1)
	 else:                #assume fixed length test
	    elo5,elo95_5,LOS5=fishtest.stats.stat_util.get_elo(results5_)
	    elo5_l=elo5-elo95_5
	    elo5_u=elo5+elo95_5
	 %>
      <H4> Pentanomial statistics</H4>
      <H5> Basic statistics </H5>
      <table class="table table-condensed">
	<tr><td>Elo</td><td>${"%.4f [%.4f, %.4f]"%(elo5,elo5_l,elo5_u)}</td></tr>
	<tr><td>LOS(1-p)</td><td>${"%.5f"%LOS5}</td></tr>
% if has_sprt:
	<tr><td>LLR</td><td>${"%.4f [%.4f, %.4f]"%(LLR5,LLR5_l,LLR5_u)}</td></tr>
% endif  ## has_sprt
      </table>
% if has_sprt:
      <H5> Generalized Log Likelihood Ratio </H5>
      <em> The monikers Alt and Alt2 are from the source code. Alt (which is 
      no longer used) is faster to compute than the exact LLR which requires
      numerically solving a rational equation.
      The simple Alt2 is used for Elo estimation.
      Note that we are not aware of any literature
      indicating that any of these LLR quantities is theoretically better than the others.
      </em>
      <table class="table table-condensed" style="margin-top:1em;">
      	<tr><td>Exact</td><td>${"%.5f"%LLR5_exact}</td></tr>
      	<tr><td>Alt</td><td>${"%.5f"%LLR5_alt}</td></tr>
      	<tr><td>Alt2</td><td>${"%.5f"%LLR5_alt2}</td></tr>
      </table>
% endif ## has_sprt
      <H5> Auxilliary statistics </H5>	
      <table class="table table-condensed">	
	<tr><td>Games</td><td>${int(games5)}</td></tr>
	<tr><td>Results(0-2)</td><td>${results5}</td></tr>
	<tr><td>Distribution</td><td>${pdf5_s}</td></tr>
	<tr><td>(DD,WL) split</td><td>${"(%.5f, %.5f)"%(results5_DD_prob,results5_WL_prob)}</td></tr>
	<tr><td>Expected value</td><td>${"%.5f"%avg5}</td></tr>
	<tr><td>Variance</td><td>${"%.5f"%var5}</td></tr>
	<tr><td>Skewness</td><td>${"%.5f"%skewness5}</td></tr>
	<tr><td>Excess kurtosis</td><td>${"%.5f"%exkurt5}</td></tr>
% if has_sprt:
	<tr><td>Score</td><td>${"%.5f"%(avg5)}</td></tr>
% else:  
	<tr><td>Score</td><td>${"%.5f [%.5f, %.5f]"%(avg5,avg5_l,avg5_u)}</td></tr>
% endif ## has_sprt
	<tr><td>Variance/game</td><td>${"%.5f [%.5f, %.5f]"%(var5_per_game,var5_per_game_l,var5_per_game_u)}</td></tr>
	<tr><td>Stdev/game</td><td>${"%.5f [%.5f, %.5f]"%(stdev5_per_game,stdev5_per_game_l,stdev5_per_game_u)}</td></tr>
% if has_sprt:
	<tr><td>Sensitivity ((score-0.5)/stdev)/game</td><td>${"%.5f"%(sens5_per_game)}</td></tr>
% else:
	<tr><td>Sensitivity ((score-0.5)/stdev)/game</td><td>${"%.5f [%.5f, %.5f]"%(sens5_per_game,sens5_per_game_l,sens5_per_game_u)}</td></tr>
% endif  ## has_sprt
% if has_sprt:
	<tr><td>Expected overshoot [H0,H1]</td><td>${"[%.5f, %.5f]"%(o0,o1)}</td></tr>
% endif  ## has_sprt
      </table>
% endif  ## has_pentanomial
      <H4> Trinomial statistics</H4>
% if has_pentanomial:
     <p>
      <em> The following quantities are computed using the incorrect trinomial model and so they should
      be taken with a grain of salt. The trinomial quantities are listed because they serve as a sanity check
      for the correct pentanomial quantities and moreover it is possible to extract some genuinely
      interesting information from the comparison between the two. </em>
      </p>
% endif  ## has_pentanomial
      <%
	 N3,pdf3=fishtest.stats.LLRcalc.results_to_pdf(results3)
	 pdf3_s=pdf_to_string(pdf3)
	 games3=N3
         avg3,var3,skewness3,exkurt3=fishtest.stats.LLRcalc.stats_ex(pdf3)
	 avg3_l=avg3-z975*(var3/N3)**.5
	 avg3_u=avg3+z975*(var3/N3)**.5
	 var3_l=var3*(1-z975*((exkurt3+2)/N3)**.5)
	 var3_u=var3*(1+z975*((exkurt3+2)/N3)**.5)
	 stdev3=var3**.5
	 stdev3_l=var3_l**.5 if var3_l>=0 else 0.0
	 stdev3_u=var3_u**.5
	 sens3,norm_var_sens3=sensitivity_conf(avg3,var3,skewness3,exkurt3)
	 sens3_l=sens3-z975*(norm_var_sens3/N3)**.5
	 sens3_u=sens3+z975*(norm_var_sens3/N3)**.5
	 sens3_per_game=sens3
	 sens3_per_game_l=sens3_l
	 sens3_per_game_u=sens3_u
	 R3_=copy.deepcopy(run['results'])
	 if has_pentanomial:
	    	 del R3_['pentanomial']
		 ratio=var5_per_game/var3
		 var_diff=var3-var5_per_game
		 RMS_bias=var_diff**.5 if var_diff>=0 else 0
		 RMS_bias_elo=fishtest.stats.stat_util.elo(0.5+RMS_bias)
	 if has_sprt:
		 sp=fishtest.stats.sprt.sprt(alpha=alpha,beta=beta,elo0=lelo0,elo1=lelo1)
		 sp.set_state(results3_)
		 a3=sp.analytics()
		 LLR3_l=a3['a']
		 LLR3_u=a3['b']
		 LLR3=fishtest.stats.LLRcalc.LLR_logistic(lelo0,lelo1,results3_)
	 	 elo3_l=a3['ci'][0]
	 	 elo3_u=a3['ci'][1]
	 	 elo3=a3['elo']
	 	 LOS3=a3['LOS']
		 # auxilliary
	         LLR3_exact=N3*fishtest.stats.LLRcalc.LLR(pdf3,score0,score1)
	         LLR3_alt  =N3*fishtest.stats.LLRcalc.LLR_alt(pdf3,score0,score1)
	         LLR3_alt2 =N3*fishtest.stats.LLRcalc.LLR_alt2(pdf3,score0,score1)
		 LLR3_be   =fishtest.stats.stat_util.LLRlegacy(belo0,belo1,results3_)
	 else:                #assume fixed length test
	 	elo3,elo95_3,LOS3=fishtest.stats.stat_util.get_elo(results3_)
		elo3_l=elo3-elo95_3
		elo3_u=elo3+elo95_3
	 %>
     <H5> Basic statistics</H5>
      <table class="table table-condensed">
	<tr><td>Elo</td><td>${"%.4f [%.4f, %.4f]"%(elo3,elo3_l,elo3_u)}</td></tr>
	<tr><td>LOS(1-p)</td><td>${"%.5f"%LOS3}</td></tr>
% if has_sprt:
	<tr><td>LLR</td><td>${"%.4f [%.4f, %.4f]"%(LLR3,LLR3_l,LLR3_u)}</td></tr>
% endif  ## has_sprt
      </table>
% if has_sprt:
       <H5> Generalized Log Likelihood Ratio </H5>
       <em> BayesElo is the LLR as computed using the BayesElo model. It is not clear how to
       generalize it to the pentanomial case. </em>
       <table class="table table-condensed" style="margin-top:1em;">
       	<tr><td>Exact</td><td>${"%.5f"%LLR3_exact}</td></tr>
       	<tr><td>Alt</td><td>${"%.5f"%LLR3_alt}</td></tr>
      	<tr><td>Alt2</td><td>${"%.5f"%LLR3_alt2}</td></tr>
	<tr><td>BayesElo</td><td>${"%.5f"%LLR3_be}</td></tr>	
      </table>
% endif  ## has_sprt
     <H5> Auxilliary statistics</H5>
      <table class="table table-condensed">
	<tr><td>Games</td><td>${int(games3)}</td></tr>
	<tr><td>Results [losses, draws, wins]</td><td>${results3}</td></tr>
	<tr><td>Distribution {loss ratio, draw ratio, win ratio}</td><td>${pdf3_s}</td></tr>
	<tr><td>Expected value</td><td>${"%.5f"%avg3}</td></tr>
	<tr><td>Variance</td><td>${"%.5f"%var3}</td></tr>
	<tr><td>Skewness</td><td>${"%.5f"%skewness3}</td></tr>
	<tr><td>Excess kurtosis</td><td>${"%.5f"%exkurt3}</td></tr>
% if has_sprt:
	<tr><td>Score</td><td>${"%.5f"%(avg3)}</td></tr>
% else:
	<tr><td>Score</td><td>${"%.5f [%.5f, %.5f]"%(avg3,avg3_l,avg3_u)}</td></tr>
% endif  ## has_sprt
	<tr><td>Variance/game</td><td>${"%.5f [%.5f, %.5f]"%(var3,var3_l,var3_u)}</td></tr>
	<tr><td>Stdev/game</td><td>${"%.5f [%.5f, %.5f]"%(stdev3,stdev3_l,stdev3_u)}</td></tr>
% if has_sprt:
	<tr><td>Sensitivity ((score-0.5)/stdev)/game</td><td>${"%.5f"%(sens3_per_game)}</td></tr>
% else:
	<tr><td>Sensitivity ((score-0.5)/stdev)/game</td><td>${"%.5f [%.5f, %.5f]"%(sens3_per_game,sens3_per_game_l,sens3_per_game_u)}</td></tr>
% endif  ## has_sprt
      </table>
% if has_pentanomial:
      <H4> Comparison</H4>
      	   <table class="table table-condensed">
		<tr><td>Variance ratio (pentanomial/trinomial)</td><td>${"%.5f"%ratio}</td></tr>
	   	<tr><td>Variance difference (trinomial-pentanomial)</td><td>${"%.5f"%var_diff}</td></tr>
	   	<tr><td>RMS bias</td><td>${"%.5f"%RMS_bias}</td></tr>
	        <tr><td>RMS bias (Elo)</td><td>${"%.3f"%RMS_bias_elo}</td></tr>
	   </table>
% endif  ## has_pentanomial
      </div>
      <div class="span2">
      </div>
% else:  ## not has_spsa / has_spsa
No statistics for spsa tests.
% endif  ## has_spsa
  </body>
</html>
