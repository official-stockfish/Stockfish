"use strict";

/*
  This program computes passing probabilities and expected running times for SPRT tests.
  See http://hardy.uhasselt.be/Fishtest/sprta.pdf for more information.
*/

function L(x) {
    return 1/(1+Math.pow(10,-x/400));
}

function PT(LA,LB,h){
    // Universal functions
    var P,T;
    if(Math.abs(h*(LA-LB))<1e-6){
	// avoid division by zero
        P=-LA/(LB-LA);
        T=-LA*LB;
    }else{
        var exp_a=Math.exp(-h*LA);
        var exp_b=Math.exp(-h*LB);
        P=(1-exp_a)/(exp_b-exp_a);
        T=(2/h)*(LB*P+LA*(1-P));
    }
    return [P,T];
}

function Sprt(alpha,beta,elo0,elo1,draw_ratio,rms_bias) {
    this.score0=L(elo0);
    this.score1=L(elo1);
    var rms_bias_score=L(rms_bias)-0.5;
    var variance3=(1-draw_ratio)/4.0;
    this.variance=variance3-Math.pow(rms_bias_score,2);
    this.w2=Math.pow((this.score1-this.score0),2)/this.variance;
    this.LA=Math.log(beta/(1-alpha));
    this.LB=Math.log((1-beta)/alpha);
    this.characteristics=function(elo){
	var score=L(elo);
	var h=(2*score-(this.score0+this.score1))/(this.score1-this.score0);
	var PT_=PT(this.LA,this.LB,h);
	return [PT_[0],PT_[1]/this.w2];
    }
}

