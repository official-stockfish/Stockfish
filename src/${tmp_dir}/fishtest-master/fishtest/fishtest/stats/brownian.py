from __future__ import division

import math

import scipy.stats

def Phi(x):
    """
Cumulative standard normal distribution.
"""
    return scipy.stats.norm.cdf(x)

def U(n,gamma,A,y):
    """ 
This is a primitive function of e^(gamma y)sin ((n pi y)/A),
multiplied by 2/A*exp(-gamma*y).
"""
    return (2*A*gamma*math.sin(math.pi*n*y/A) - 2*math.pi*n*math.cos (math.pi*n*y/A))/(A**2*gamma**2 + math.pi**2 * n**2)

class Brownian:

    def __init__(self,a=-1.0,b=1.0,mu=0.0,sigma=0.005):
        self.a=a
        self.b=b
        self.mu=mu
        self.sigma=sigma
        self.sigma2=sigma**2
        gamma=self.mu/self.sigma2


    def outcome_cdf(self,T=None,y=None):
        # in case of slow convergence use Siegmund approximation.
        sigma2=self.sigma2
        mu=self.mu
        gamma=mu/sigma2
        A=self.b-self.a
        if sigma2*T/A**2<1e-2 or abs(gamma*A)>15:
            ret=self.outcome_cdf_alt2(T,y)
        else:
            ret=self.outcome_cdf_alt1(T,y)
        assert -1e-3 <= ret <= 1+1e-3
        return ret
        
    def outcome_cdf_alt1(self,T=None,y=None):
        """ 
Computes the probability that the particle passes to the
right of (T,y), the time axis being vertically oriented.
This may give a numerical exception if math.pi**2*sigma2*T/(2*A**2) 
is small.
"""
        mu=self.mu
        sigma2=self.sigma2
        A=self.b-self.a
        x=0-self.a
        y=y-self.a
        gamma=mu/sigma2
        n=1
        s=0.0
        lambda_1=((math.pi/A)**2)*sigma2/2+(mu**2/sigma2)/2
        t0=math.exp(-lambda_1*T-x*gamma+y*gamma)
        while True:
            lambda_n=((n*math.pi/A)**2)*sigma2/2+(mu**2/sigma2)/2
            t1=math.exp(-(lambda_n-lambda_1)*T)
            t3=U(n,gamma,A,y)
            t4=math.sin(n*math.pi*x/A)
            s+=t1*t3*t4
            if abs(t0*t1*t3)<=1e-9:
                break
            n+=1
        if gamma*A>30:     # avoid numerical overflow
            pre=math.exp(-2*gamma*x)
        elif abs(gamma*A)<1e-8: # avoid division by zero
            pre=(A-x)/A
        else:
            pre=(1-math.exp(2*gamma*(A-x)))/(1-math.exp(2*gamma*A))
        return pre+t0*s

    def outcome_cdf_alt2(self,T=None,y=None):
        """
Siegmund's approximation. We use it as backup if our
exact formula converges too slowly. To make the evaluation
robust we use the asymptotic development of Phi.
"""
        denom=math.sqrt(T*self.sigma2)
        offset=self.mu*T
        gamma=self.mu/self.sigma2
        a=self.a
        b=self.b
        z=(y-offset)/denom
        za=(-y+offset+2*a)/denom
        zb=(y-offset-2*b)/denom
        t1=Phi(z)
        if gamma*a>=5:
            t2=-math.exp(-za**2/2+2*gamma*a)/math.sqrt(2*math.pi)*(1/za-1/za**3)
        else:
            t2=math.exp(2*gamma*a)*Phi(za)
        if gamma*b>=5:
            t3=-math.exp(-zb**2/2+2*gamma*b)/math.sqrt(2*math.pi)*(1/zb-1/zb**3)
        else:
            t3=math.exp(2*gamma*b)*Phi(zb)
        return t1+t2-t3

    


