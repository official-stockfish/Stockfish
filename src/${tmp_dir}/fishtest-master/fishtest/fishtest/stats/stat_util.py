from __future__ import division

import math,copy

import scipy.stats

from fishtest.stats import LLRcalc
from fishtest.stats import sprt
from fishtest.stats import brownian

def Phi(q):
  """
Cumlative distribution function for the standard Gaussian law: quantile -> probability
"""
  return scipy.stats.norm.cdf(q)

def Phi_inv(p):
  """
Quantile function for the standard Gaussian law: probability -> quantile
"""
  return scipy.stats.norm.ppf(p)

def elo(x):
  epsilon=1e-3
  x=max(x,epsilon)
  x=min(x,1-epsilon)
  return -400*math.log10(1/x-1)

def L(x):
    return 1/(1+10**(-x/400.0))

def stats(results):
  """
"results" is an array of length 2*n+1 with aggregated frequences
for n games.
"""
  l=len(results)
  N=sum(results)
  games=N*(l-1)/2.0

# empirical expected score for a single game
  mu=sum([results[i]*(i/2.0) for i in range(0,l)])/games

# empirical expected variance for a single game
  mu_=(l-1)/2.0*mu
  var=sum([results[i]*(i/2.0-mu_)**2.0 for i in range(0,l)])/games

  return games,mu,var

def get_elo(results):
  """
"results" is an array of length 2*n+1 with aggregated frequences
for n games.
"""
  results=LLRcalc.regularize(results)
  games,mu,var=stats(results)
  stdev = math.sqrt(var)

# 95% confidence interval for mu
  mu_min=mu+Phi_inv(0.025)*stdev/math.sqrt(games)
  mu_max=mu+Phi_inv(0.975)*stdev/math.sqrt(games)

  el=elo(mu)
  elo95=(elo(mu_max)-elo(mu_min))/2.0
  los = Phi((mu-0.5)/(stdev/math.sqrt(games)))

  return el,elo95,los


def bayeselo_to_proba(elo, drawelo):
  """
elo is expressed in BayesELO (relative to the choice drawelo).
Returns a probability, P[2], P[0], P[1] (win,loss,draw).
"""
  P = 3*[0]
  P[2] = 1.0 / (1.0 + pow(10.0, (-elo + drawelo) / 400.0))
  P[0] = 1.0 / (1.0 + pow(10.0, (elo + drawelo) / 400.0))
  P[1] = 1.0 - P[2] - P[0]
  return P

def proba_to_bayeselo(P):
  """
Takes a probability: P[2], P[0]
Returns elo, drawelo.
"""
  assert(0 < P[2] and P[2] < 1 and 0 < P[0] and P[0] < 1)
  elo = 200 * math.log10(P[2]/P[0] * (1-P[0])/(1-P[2]))
  drawelo = 200 * math.log10((1-P[0])/P[0] * (1-P[2])/P[2])
  return elo, drawelo

def draw_elo_calc(R):
  """
Takes trinomial frequences R[0],R[1],R[2]
(loss,draw,win) and returns the corresponding
drawelo value.
"""
  N=sum(R)
  P=[p/N for p in R]
  _, drawelo = proba_to_bayeselo(P)
  return drawelo

def bayeselo_to_elo(belo, drawelo):
  P = bayeselo_to_proba(belo, drawelo)
  return elo(P[2]+0.5*P[1])

def elo_to_bayeselo(elo, draw_ratio):
  assert(draw_ratio>=0)
  s=L(elo)
  P=3*[0]
  P[2]=s-draw_ratio/2.0
  P[1]=draw_ratio
  P[0]=1-P[1]-P[2]
  if P[0]<=0 or P[2]<=0:
    return float('NaN'),float('NaN')
  return proba_to_bayeselo(P)

def SPRT_elo(R, alpha=0.05, beta=0.05, p=0.05, elo0=None, elo1=None, elo_model=None):
  """
Calculate an elo estimate from an SPRT test.
"""
  assert(elo_model in ['BayesElo','logistic'])

  # Estimate drawelo out of sample
  R3=LLRcalc.regularize([R['losses'],R['draws'],R['wins']])
  drawelo=draw_elo_calc(R3)

  # Convert the bounds to logistic elo if necessary
  if elo_model=='BayesElo':
    lelo0,lelo1=[bayeselo_to_elo(elo_, drawelo) for elo_ in (elo0,elo1)]
  else:
    lelo0,lelo1=elo0,elo1

  # Make the elo estimation object
  sp=sprt.sprt(alpha=alpha,beta=beta,elo0=lelo0,elo1=lelo1)

  # Feed the results
  if 'pentanomial' in R.keys():
    R_=R['pentanomial']
  else:
    R_=R3
  sp.set_state(R_)

  # Get the elo estimates
  a=sp.analytics(p)

  # Override the LLR approximation with the exact one
  a['LLR']=LLRcalc.LLR_logistic(lelo0,lelo1,R_)
  del a['clamped']
  # Now return the estimates
  return a

def LLRlegacy(belo0,belo1,results):
  """
LLR calculation using the BayesElo model where
drawelo is estimated "out of sample".
"""
  assert(len(results)==3)
  drawelo=draw_elo_calc(results)
  P0=bayeselo_to_proba(belo0,drawelo)
  P1=bayeselo_to_proba(belo1,drawelo)
  return sum([results[i]*math.log(P1[i]/P0[i]) for i in range(0,3)])


def SPRT(alpha=0.05,beta=0.05,elo0=None,elo1=None,elo_model='logistic',batch_size=1):
  """ Constructor for the "sprt object" """
  return {'alpha'       : alpha,
          'beta'        : beta,
          'elo0'        : elo0,
          'elo1'        : elo1,
          'elo_model'   : elo_model,
          'state'       : '',
          'llr'         : 0,
          'batch_size'  : batch_size,
          'lower_bound' : math.log(beta/(1-alpha)),
          'upper_bound' : math.log((1-beta)/alpha),
          'overshoot'   : {'last_update'    : 0,
                           'skipped_updates': 0,
                           'ref0'           : 0,
                           'm0'             : 0,
                           'sq0'            : 0,
                           'ref1'           : 0,
                           'm1'             : 0,
                           'sq1'            : 0}
        }

def update_SPRT(R, sprt):
  """Sequential Probability Ratio Test

sprt is a dictionary with fixed fields

'elo0', 'alpha', 'elo1', 'beta', 'elo_model', 'lower_bound', 'upper_bound', 'batch_size'

It also has the following fields

'llr', 'state', 'overshoot'

which are updated by this function.

Normally this function should be called each time 'batch_size' games (trinomial) or
game pairs (pentanomial) have been completed but it is safe to call it multiple times
with the same parameters. The main purpose of this is to be able to recalculate
the LLR for old tests.

In the unlikely event of a server crash it is possible that some updates may be missed
but this situation is also handled sensibly.

The meaning of the other inputs and the fields is as follows.

H0: elo = elo0
H1: elo = elo1
alpha = max typeI error (reached on elo = elo0)
beta = max typeII error for elo >= elo1 (reached on elo = elo1)
'overshoot' is a dictionary with data for dynamic overshoot
estimation. The theoretical basis for this is: Siegmund - Sequential
Analysis - Corollary 8.33.  The correctness can be verified by
simulation

https://github.com/vdbergh/simul

R['wins'], R['losses'], R['draws'] contains the number of wins, losses and draws
R['pentanomial'] contains the pentanomial frequencies
elo_model can be either 'BayesElo' or 'logistic'
"""

  # the next two lines are superfluous, but unfortunately necessary for backward
  # compatibility with old tests
  sprt['lower_bound']=math.log(sprt['beta']/(1-sprt['alpha']))
  sprt['upper_bound']=math.log((1-sprt['beta'])/sprt['alpha'])
  
  elo_model=sprt.get('elo_model', 'BayesElo')
  assert(elo_model in ['BayesElo','logistic'])
  elo0=sprt['elo0']
  elo1=sprt['elo1']

  # first deal with the legacy BayesElo/trinomial models
  R3=[R['losses'],R['draws'],R['wins']]
  if elo_model=='BayesElo':
    # estimate drawelo out of sample
    R3_=LLRcalc.regularize(R3)
    drawelo=draw_elo_calc(R3_)
    # conversion of bounds to logistic elo
    lelo0,lelo1=[bayeselo_to_elo(elo,drawelo) for elo in (elo0,elo1)]
  else:
    lelo0,lelo1=elo0,elo1

  R_=R.get('pentanomial',R3)

  batch_size=sprt.get('batch_size',1)

  # sanity check on batch_size
  if sum(R_)%batch_size!=0:
    sprt['illegal_update']=sum(R_)  # audit
    if 'overshoot' in sprt:
      del sprt['overshoot'] # the contract is violated

  # Log-Likelihood Ratio
  sprt['llr']=LLRcalc.LLR_logistic(lelo0,lelo1,R_)

  # update the overshoot data
  if 'overshoot' in sprt:
    LLR_=sprt['llr']
    o=sprt['overshoot']
    num_samples=sum(R_)
    if num_samples < o['last_update']: # purge?
      sprt['lost_samples']=o['last_update']-num_samples   # audit
      del sprt['overshoot'] # the contract is violated
    else:
      if num_samples==o['last_update']:  # same data
        pass
      elif num_samples==o['last_update']+batch_size:  # the normal case
        if LLR_<o['ref0']:
          delta=LLR_-o['ref0']
          o['m0']+=delta
          o['sq0']+=delta**2
          o['ref0']=LLR_
        if LLR_>o['ref1']:
          delta=LLR_-o['ref1']
          o['m1']+=delta
          o['sq1']+=delta**2
          o['ref1']=LLR_
      else:
        # Be robust if some updates are lost: reset data collection.
        # This should not be needed anymore, but just in case...
        o['ref0']=LLR_
        o['ref1']=LLR_
        o['skipped_updates']+=(num_samples-o['last_update'])-1 # audit
      o['last_update']=num_samples

  o0=0
  o1=0
  if 'overshoot' in sprt:
    o=sprt['overshoot']
    o0=-o['sq0']/o['m0']/2 if o['m0']!=0 else 0
    o1=o['sq1']/o['m1']/2 if o['m1']!=0 else 0

  # now check the stop condition
  sprt['state']=''
  if sprt['llr'] < sprt['lower_bound']+o0:
    sprt['state'] = 'rejected'
  elif sprt['llr'] > sprt['upper_bound']-o1:
    sprt['state'] = 'accepted'

if __name__ == "__main__":
  # unit tests
  print('SPRT tests')
  R={'wins': 65388,'losses': 65804, 'draws': 56553, 'pentanomial':[10789, 19328, 33806, 19402, 10543]}
  sprt_=SPRT(elo0=-3, alpha=0.05, elo1=1, beta=0.05, elo_model='logistic')
  update_SPRT(R,sprt_)
  print(sprt_)

  print('elo tests')
  print(SPRT_elo({'wins': 0, 'losses': 0, 'draws': 0}, elo0=0,  elo1=5, elo_model='BayesElo'))
  print(SPRT_elo({'wins': 10, 'losses': 0, 'draws': 0}, elo0=0,  elo1=5, elo_model='BayesElo'))
  print(SPRT_elo({'wins': 100, 'losses': 0, 'draws': 0}, elo0=0,  elo1=5, elo_model='BayesElo'))
  print(SPRT_elo({'wins': 10, 'losses': 0, 'draws': 20}, elo0=0,  elo1=5, elo_model='BayesElo'))
  print(SPRT_elo({'wins': 10, 'losses': 1, 'draws': 20}, elo0=0,  elo1=5, elo_model='BayesElo'))
  print(SPRT_elo({'wins': 5019, 'losses': 5026, 'draws': 15699}, elo0=0,  elo1=5, elo_model='BayesElo'))
  print(SPRT_elo({'wins': 1450, 'losses': 1500, 'draws': 4000}, elo0=0,  elo1=6, elo_model='BayesElo'))
  print(SPRT_elo({'wins': 716, 'losses': 591, 'draws': 2163}, elo0=0,  elo1=6, elo_model='BayesElo'))
  print(SPRT_elo({'wins': 13543,'losses': 13624, 'draws': 34333}, elo0=-3,  elo1=1, elo_model='BayesElo'))
  print(SPRT_elo({'wins': 13543,'losses': 13624, 'draws': 34333, 'pentanomial':[1187, 7410, 13475, 7378, 1164]}, elo0=-3,  elo1=1, elo_model='BayesElo'))
  print(SPRT_elo({'wins': 65388,'losses': 65804, 'draws': 56553}, elo0=-3,  elo1=1, elo_model='BayesElo'))
  print(SPRT_elo({'wins': 65388,'losses': 65804, 'draws': 56553, 'pentanomial':[10789, 19328, 33806, 19402, 10543]}, elo0=-3,  elo1=1, elo_model='BayesElo'))
  print(SPRT_elo({'wins': 65388,'losses': 65804, 'draws': 56553, 'pentanomial':[10789, 19328, 33806, 19402, 10543]}, elo0=-3,  elo1=1, elo_model='logistic'))
