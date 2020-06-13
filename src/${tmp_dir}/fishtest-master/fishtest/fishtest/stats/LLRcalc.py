from __future__ import division

import math,sys,copy

import scipy

import scipy.optimize

def MLE(pdf,s):
    """
This function computes the maximum likelood estimate for
a discrete distribution with expectation value s,
given an observed (i.e. empirical) distribution pdf.

pdf is a list of tuples (ai,pi), i=1,...,N. It is assumed that
that the ai are strictly ascending, a1<s<aN and p1>0, pN>0.

The theory behind this function can be found in the online
document

http://hardy.uhasselt.be/Fishtest/support_MLE_multinomial.pdf

(see Proposition 1.1).

"""
    epsilon=1e-9
    v,w=pdf[0][0],pdf[-1][0]
    assert(v<s<w)
    l,u=-1/(w-s),1/(s-v)
    f=lambda x:sum([p*(a-s)/(1+x*(a-s)) for a,p in pdf])
    x,res=scipy.optimize.brentq(f,
                                l+epsilon,
                                u-epsilon,
                                full_output=True,
                                disp=False)
    assert(res.converged)
    pdf_MLE=[(a,p/(1+x*(a-s))) for a,p in pdf]
    s_,var=stats(pdf_MLE) # for validation
    assert(abs(s-s_)<1e-6)
    return pdf_MLE

def stats(pdf):
    epsilon=1e-6
    for i in range(0,len(pdf)):
        assert(-epsilon<=pdf[i][1]<=1+epsilon)
    n=sum([prob for value,prob in pdf])
    assert(abs(n-1)<epsilon)
    s=sum([prob*value for value,prob in pdf])
    var=sum([prob*(value-s)**2 for value,prob in pdf])
    return s,var

def stats_ex(pdf):
    """
Computes expectation value, variance, skewness and excess
kurtosis for a discrete distribution.
"""
    s,var=stats(pdf)
    m3=sum([prob*(value-s)**3 for value,prob in pdf])
    m4=sum([prob*(value-s)**4 for value,prob in pdf])
    skewness=m3/var**1.5
    exkurt=m4/var**2-3
    return s,var,skewness,exkurt

def LLRjumps(pdf,s0,s1):
    pdf0,pdf1=[MLE(pdf,s) for s in (s0,s1)]
    return [(math.log(pdf1[i][1])-math.log(pdf0[i][1]),pdf[i][1]) for i in range(0,len(pdf))]

def LLR(pdf,s0,s1):
    """
This function computes the generalized log likelihood ratio (divided by N)
for s=s1 versus s=s0 where pdf is an empirical distribution and
s is the expectation value of the true distribution.
pdf is a list of pairs (value,probability).
"""
    return stats(LLRjumps(pdf,s0,s1))[0]

def LLR_alt(pdf,s0,s1):
    """
This function computes the approximate generalized log likelihood ratio (divided by N)
for s=s1 versus s=s0 where pdf is an empirical distribution and
s is the expectation value of the true distribution.
pdf is a list of pairs (value,probability). See

http://hardy.uhasselt.be/Fishtest/support_MLE_multinomial.pdf
"""
    r0,r1=[sum([prob*(value-s)**2 for value,prob in pdf]) for s in (s0,s1)]
    return 1/2*math.log(r0/r1)

def LLR_alt2(pdf,s0,s1):
    """
This function computes the approximate generalized log likelihood ratio (divided by N)
for s=s1 versus s=s0 where pdf is an empirical distribution and
s is the expectation value of the true distribution.
pdf is a list of pairs (value,probability). See

http://hardy.uhasselt.be/Fishtest/GSPRT_approximation.pdf
"""
    s,var=stats(pdf)
    return (s1-s0)*(2*s-s0-s1)/var/2.0

def LLR_drift_variance(pdf,s0,s1,s=None):
    """
Computes the drift and variance of the LLR
for a test s=s0 against s=s0
when the empirical distribution is pdf,
but the true value of s is as given by
the argument s. If s is not given
then it is assumed that pdf is the true
distribution.
"""
    if s!=None:
        pdf=MLE(pdf,s)
    jumps=LLRjumps(pdf,s0,s1)
    return stats(jumps)

def LLR_drift_variance_alt2(pdf,s0,s1,s=None):
    """
Computes the approximated drift and variance of the LLR
for a test s=s0 against s=s0
approximated by a Brownian motion, when
the empirical distribution is pdf,
but the true value of s is as given by
the argument s. If s is not given
the it is assumed that pdf is the true
distribution. See

http://hardy.uhasselt.be/Fishtest/GSPRT_approximation.pdf
"""
    s_,v_=stats(pdf)
    # replace v_ by its MLE if requested
    s,v=(s_,v_) if s==None else (s,v_+(s-s_)**2)
    mu=(s-(s0+s1)/2)*(s1-s0)/v
    var=(s1-s0)**2/v
    return mu,var

def L_(x):
    return 1/(1+10**(-x/400))

def regularize(l):
    """
If necessary mix in a small prior for regularization.
"""
    epsilon=1e-3
    l=copy.copy(l)
    for i in range(0,len(l)):
        if l[i]==0:
            l[i]=epsilon
    return l

def results_to_pdf(results):
    results=regularize(results)
    N=sum(results)
    l=len(results)
    return N,[(i/(l-1),results[i]/N) for i in range(0,l)]

def LLR_logistic(elo0,elo1,results):
    """
This function computes the generalized log-likelihood ratio for "results"
which should be a list of either length 3 or 5. If the length
is 3 then it should contain the frequencies of L,D,W. If the length
is 5 then it should contain the frequencies of the game pairs
LL,LD+DL,LW+DD+WL,DW+WD,WW.
elo0,elo1 are in logistic elo.
"""
    s0,s1=[L_(elo) for elo in (elo0,elo1)]
    N,pdf=results_to_pdf(results)
    return N*LLR(pdf,s0,s1)


