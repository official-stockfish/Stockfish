from __future__ import division

import math,copy
import argparse

import scipy.optimize

from fishtest.stats.brownian import Brownian
from fishtest.stats import LLRcalc

class sprt:
    def __init__(self,alpha=0.05,beta=0.05,elo0=0,elo1=5):
        self.a=math.log(beta/(1-alpha))
        self.b=math.log((1-beta)/alpha)
        self.elo0=elo0
        self.elo1=elo1
        self.s0=LLRcalc.L_(elo0)
        self.s1=LLRcalc.L_(elo1)
        self.clamped=False
        self.LLR_drift_variance=LLRcalc.LLR_drift_variance_alt2

    def set_state(self,results):
        N,self.pdf=LLRcalc.results_to_pdf(results)
        mu_LLR,var_LLR=self.LLR_drift_variance(self.pdf,self.s0,self.s1,None)

        # llr estimate
        self.llr=N*mu_LLR
        self.T=N

        # now normalize llr (if llr is not legal then the implications
        # of this are unclear)
        slope=self.llr/N
        if self.llr>1.03*self.b or self.llr<1.03*self.a:
            self.clamped=True
        if self.llr<self.a:
            self.T=self.a/slope
            self.llr=self.a
        elif self.llr>self.b:
            self.T=self.b/slope
            self.llr=self.b

    def outcome_prob(self,elo):
        """
The probability of a test with the given elo with worse outcome
(faster fail, slower pass or a pass changed into a fail).
"""
        s=LLRcalc.L_(elo)
        mu_LLR,var_LLR=self.LLR_drift_variance(self.pdf,self.s0,self.s1,s)
        sigma_LLR=math.sqrt(var_LLR)
        return Brownian(a=self.a,b=self.b,mu=mu_LLR,sigma=sigma_LLR).outcome_cdf(T=self.T,y=self.llr)

    def lower_cb(self,p):
        """
Maximal elo value such that the observed outcome of the test has probability
less than p.
"""
        avg_elo=(self.elo0+self.elo1)/2
        delta=self.elo1-self.elo0
        N=30
# Various error conditions must be handled better here!
        while True:
            elo0=max(avg_elo-N*delta,-1000)
            elo1=min(avg_elo+N*delta,1000)
            try:
                sol,res=scipy.optimize.brentq(lambda elo:self.outcome_prob(elo)-(1-p),
                                              elo0,
                                              elo1,
                                              full_output=True,
                                              disp=False)
            except ValueError:
                if elo0>-1000 or elo1<1000:
                    N*=2
                    continue
                else:
                    if self.outcome_prob(elo0)-(1-p)>0:
                        return elo1
                    else:
                        return elo0
            assert(res.converged)
            break
        return sol

    def analytics(self,p=0.05):
        ret={}
        ret['clamped']=self.clamped
        ret['a']=self.a
        ret['b']=self.b
        ret['elo']=self.lower_cb(0.5)
        ret['ci']=[self.lower_cb(p/2),self.lower_cb(1-p/2)]
        ret['LOS']=self.outcome_prob(0)
        ret['LLR']=self.llr
        return ret

if __name__=='__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("--alpha",help="probability of a false positve",type=float,default=0.05)
    parser.add_argument("--beta" ,help="probability of a false negative",type=float,default=0.05)
    parser.add_argument("--elo0", help="H0 (expressed in LogisticElo)",type=float,default=0.0)
    parser.add_argument("--elo1", help="H1 (expressed in LogisticElo)",type=float,default=5.0)
    parser.add_argument("--level",help="confidence level",type=float,default=0.95)
    parser.add_argument("--results", help="trinomial of pentanomial frequencies, low to high",nargs="*",type=int, required=True)
    args=parser.parse_args()
    results=args.results
    if len(results)!=3 and len(results)!=5:
        parser.error("argument --results: expected 3 or 5 arguments")
    alpha=args.alpha
    beta=args.beta
    elo0=args.elo0
    elo1=args.elo1
    p=1-args.level
    s=sprt(alpha=alpha,beta=beta,elo0=elo0,elo1=elo1)
    s.set_state(results)
    a=s.analytics(p)
    print("Design parameters")
    print("=================")
    print("False positives             :  %4.2f%%" % (100*alpha,))
    print("False negatives             :  %4.2f%%" % (100*beta,) )
    print("[Elo0,Elo1]                 :  [%.2f,%.2f]"  % (elo0,elo1))
    print("Confidence level            :  %4.2f%%" % (100*(1-p),))
    print("Estimates")
    print("=========")
    print("Elo                         :  %.2f"    % a['elo'])
    print("Confidence interval         :  [%.2f,%.2f] (%4.2f%%)"  % (a['ci'][0],a['ci'][1],100*(1-p)))
    print("LOS                         :  %4.2f%%" % (100*a['LOS'],))
    print("Context")
    print("=======")
    print("LLR [u,l]                   :  %.2f %s [%.2f,%.2f]"       % (a['LLR'], '(clamped)' if a['clamped'] else '',a['a'],a['b']))
