/*
 *       $Id$
 *
 *       This source code is part of
 *
 *        G   R   O   M   A   C   S
 *
 * GROningen MAchine for Chemical Simulations
 *
 *            VERSION 2.0
 * 
 * Copyright (c) 1991-1997
 * BIOSON Research Institute, Dept. of Biophysical Chemistry
 * University of Groningen, The Netherlands
 * 
 * Please refer to:
 * GROMACS: A message-passing parallel molecular dynamics implementation
 * H.J.C. Berendsen, D. van der Spoel and R. van Drunen
 * Comp. Phys. Comm. 91, 43-56 (1995)
 *
 * Also check out our WWW page:
 * http://rugmd0.chem.rug.nl/~gmx
 * or e-mail to:
 * gromacs@chem.rug.nl
 *
 * And Hey:
 * GROup of MAchos and Cynical Suckers
 */
static char *SRCID_bondfree_c = "$Id$";

#include <math.h>
#include "assert.h"
#include "physics.h"
#include "vec.h"
#include "vveclib.h"
#include "maths.h"
#include "txtdump.h"
#include "bondf.h"
#include "pbc.h"
#include "ns.h"
#include "macros.h"
#include "names.h"
#include "fatal.h"
#include "mshift.h"
#include "inloop.h"
#include "main.h"
#include "disre.h"
#include "led.h"

static bool bPBC=FALSE;

void pbc_rvec_sub(matrix box,rvec xi,rvec xj,rvec dx)
{
  if (bPBC)
    pbc_dx(box,xi,xj,dx);
  else
    rvec_sub(xi,xj,dx);
}

void calc_bonds(FILE *log,t_idef *idef,
		rvec x_s[],rvec f[],
		t_forcerec *fr,t_graph *g,
		real epot[],t_nrnb *nrnb,
		matrix box,real lambda,
		t_mdatoms *md,int ngrp,real egnb[],real egcoul[])
{
  static bool bFirst=TRUE;
  int    ftype,nbonds,ind,nat;

  if (bFirst) {
    bPBC   = (getenv("PBC") != NULL);
    fprintf(log,"Full PBC calculation = %s\n",bool_names[bPBC]);
    bFirst = FALSE;
#ifdef DEBUG
    p_graph(log,"Bondage is fun",g);
#endif
  }
  for(ftype=0; (ftype<F_NRE); ftype++) {
    if (interaction_function[ftype].flags & IF_BOND) {
      nbonds=idef->il[ftype].nr;
      if (nbonds > 0) {
	epot[ftype]+=
	  interaction_function[ftype].ifunc(log,nbonds,idef->il[ftype].iatoms,
					    idef->iparams,x_s,f,fr,g,box,
					    lambda,&epot[F_DVDL],
					    md,ngrp,egnb,egcoul);
	ind = interaction_function[ftype].nrnb_ind;
	nat = interaction_function[ftype].nratoms+1;
	if (ind != -1)
	  inc_nrnb(nrnb,ind,nbonds/nat);
      }
    }
  }
}

/*
 * Morse potential bond by Frank Everdij
 *
 * Three parameters needed:
 *
 * b0 = equilibrium distance in nm
 * be = beta in nm^-1 (actually, it's nu_e*Sqrt(2*pi*pi*mu/D_e))
 * cb = well depth in kJ/mol
 *
 * Note: the potential is referenced to be +cb at infinite separation
 *       and zero at the equilibrium distance!
 */

real morsebonds(FILE *log,int nbonds,
                t_iatom forceatoms[],t_iparams forceparams[],
                rvec x[],rvec f[],t_forcerec *fr,t_graph *g,
                matrix box,real lambda,real *dvdl,
                t_mdatoms *md,int ngrp,real egnb[],real egcoul[])
{
  const real one=1.0;
  const real two=2.0;
  real  dr,dr2,temp,omtemp,cbomtemp,fbond,vbond,fij,b0,be,cb,vtot;
  rvec  dx;
  int   i,m,ki,kj,type,ai,aj;

  vtot = 0.0;
  for(i=0; (i<nbonds); ) {
    type = forceatoms[i++];
    ai   = forceatoms[i++];
    aj   = forceatoms[i++];
    
    b0   = forceparams[type].morse.b0;
    be   = forceparams[type].morse.beta;
    cb   = forceparams[type].morse.cb;

    pbc_rvec_sub(box,x[ai],x[aj],dx);               /*   3          */
    dr2  = iprod(dx,dx);                            /*   5          */
    dr   = sqrt(dr2);                               /*  10          */
    temp = exp(-be*(dr-b0));                        /*  12          */
    
    if (temp == one)
      continue;

    omtemp   = one-temp;                               /*   1          */
    cbomtemp = cb*omtemp;                              /*   1          */
    vbond    = cbomtemp*omtemp;                        /*   1          */
    fbond    = -two*be*temp*cbomtemp*invsqrt(dr2);      /*   9          */
    vtot    += vbond;       /* 1 */
    
    ki=SHIFT_INDEX(g,ai);
    kj=SHIFT_INDEX(g,aj);
    for (m=0; (m<DIM); m++) {                          /*  15          */
      fij=fbond*dx[m];
      f[ai][m]+=fij;
      f[aj][m]-=fij;
      fr->fshift[ki][m]+=fij;
      fr->fshift[kj][m]-=fij;
    }
  }                                           /*  58 TOTAL    */
  return vtot;
}

real harmonic(real kA,real kB,real xA,real xB,real x,real lambda,
	      real *V,real *F)
{
  const real half=0.5;
  real  L1,kk,x0,dx,dx2;
  real  v,f,dvdl;
  
  L1    = 1.0-lambda;
  kk    = L1*kA+lambda*kB;
  x0    = L1*xA+lambda*xB;
  
  dx    = x-x0;
  dx2   = dx*dx;
  
  f     = -kk*dx;
  v     = half*kk*dx2;
  dvdl  = half*(kB-kA)*dx2 + (xA-xB)*kk*dx;
  
  *F    = f;
  *V    = v;
  
  return dvdl;
  
  /* That was 19 flops */
}


real bonds(FILE *log,int nbonds,
	   t_iatom forceatoms[],t_iparams forceparams[],
	   rvec x[],rvec f[],t_forcerec *fr,t_graph *g,
	   matrix box,real lambda,real *dvdlambda,
	   t_mdatoms *md,int ngrp,real egnb[],real egcoul[])
{
  int  i,m,ki,kj,ai,aj,type;
  real dr,dr2,fbond,vbond,fij,vtot;
  rvec dx;

  vtot = 0.0;
  for(i=0; (i<nbonds); ) {
    type = forceatoms[i++];
    ai   = forceatoms[i++];
    aj   = forceatoms[i++];
  
    pbc_rvec_sub(box,x[ai],x[aj],dx);			/*   3 		*/
    dr2=iprod(dx,dx);				/*   5		*/
    dr=sqrt(dr2);					/*  10		*/

    *dvdlambda += harmonic(forceparams[type].harmonic.krA,
			   forceparams[type].harmonic.krB,
			   forceparams[type].harmonic.rA,
			   forceparams[type].harmonic.rB,
			   dr,lambda,&vbond,&fbond);

    if (dr2 == 0.0)
      continue;
    
    vtot  += vbond;/* 1*/
    fbond *= invsqrt(dr2);			/*   6		*/
#ifdef DEBUG
    if (debug)
      fprintf(debug,"BONDS: dr = %10g  vbond = %10g  fbond = %10g\n",
	      dr,vbond,fbond);
#endif
    ki=SHIFT_INDEX(g,ai);
    kj=SHIFT_INDEX(g,aj);
    for (m=0; (m<DIM); m++) {			/*  15		*/
      fij=fbond*dx[m];
      f[ai][m]+=fij;
      f[aj][m]-=fij;
      fr->fshift[ki][m]+=fij;
      fr->fshift[kj][m]-=fij;
    }
  }					/* 44 TOTAL	*/
  return vtot;
}

real water_pol(FILE *log,int nbonds,
	       t_iatom forceatoms[],t_iparams forceparams[],
	       rvec x[],rvec f[],t_forcerec *fr,t_graph *g,
	       matrix box,real lambda,real *dvdlambda,
	       t_mdatoms *md,int ngrp,real egnb[],real egcoul[])
{
  /* This routine implements anisotropic polarizibility for water, through
   * a shell connected to a dummy with spring constant that differ in the
   * three spatial dimensions in the molecular frame.
   */
  int  i,m,aO,aH1,aH2,aD,aS,type;
  rvec dOH1,dOH2,dHH,dOD,dDS,nW,kk,dx,kdx,proj;
#ifdef DEBUG
  rvec df;
#endif
  real vtot,fij,r_HH,r_OD,r_nW,tx,ty,tz;
  
  vtot = 0.0;
  if (nbonds > 0) {
    type   = forceatoms[0];
    kk[XX] = forceparams[type].wpol.kx;
    kk[YY] = forceparams[type].wpol.ky;
    kk[ZZ] = forceparams[type].wpol.kz;
    r_HH   = 1.0/forceparams[type].wpol.rHH;
    r_OD   = 1.0/forceparams[type].wpol.rOD;
    if (debug) {
      fprintf(debug,"WPOL: kk  = %10.3f        %10.3f        %10.3f\n",
	      kk[XX],kk[YY],kk[ZZ]);
      fprintf(debug,"WPOL: rOH = %10.3f  rHH = %10.3f  rOD = %10.3f\n",
	      forceparams[type].wpol.rOH,
	      forceparams[type].wpol.rHH,
	      forceparams[type].wpol.rOD);
    }
    for(i=0; (i<nbonds); ) {
      type = forceatoms[i++];
      aO   = forceatoms[i++];
      aH1  = aO+1;
      aH2  = aO+2;
      aD   = aO+3;
      aS   = aO+4;
      
      /* Compute vectors describing the water frame */
      rvec_sub(x[aH1],x[aO], dOH1);
      rvec_sub(x[aH2],x[aO], dOH2);
      rvec_sub(x[aH2],x[aH1],dHH);
      rvec_sub(x[aD], x[aO], dOD);
      rvec_sub(x[aS], x[aD], dDS);
      oprod(dOH1,dOH2,nW);
      
      /* Compute inverse length of normal vector 
       * (this one could be precomputed, but I'm too lazy now)
       */
      r_nW = invsqrt(iprod(nW,nW));
      /* This is for precision, but does not make a big difference,
       * it can go later.
       */
      r_OD = invsqrt(iprod(dOD,dOD)); 
      
      /* Normalize the vectors in the water frame */
      svmul(r_nW,nW,nW);
      svmul(r_HH,dHH,dHH);
      svmul(r_OD,dOD,dOD);
      
      /* Compute displacement of shell along components of the vector */
      dx[ZZ] = iprod(dDS,dOD);
      /* Compute projection on the XY plane: dDS - dx[ZZ]*dOD */
      for(m=0; (m<DIM); m++)
	proj[m] = dDS[m]-dx[ZZ]*dOD[m];
      
      /*dx[XX] = iprod(dDS,nW);
	dx[YY] = iprod(dDS,dHH);*/
      dx[XX] = iprod(proj,nW);
      for(m=0; (m<DIM); m++)
	proj[m] -= dx[XX]*nW[m];
      dx[YY] = iprod(proj,dHH);
      /*#define DEBUG*/
#ifdef DEBUG
      if (debug) {
	fprintf(debug,"WPOL: dx2=%10g  dy2=%10g  dz2=%10g  sum=%10g  dDS^2=%10g\n",
		sqr(dx[XX]),sqr(dx[YY]),sqr(dx[ZZ]),iprod(dx,dx),iprod(dDS,dDS));
	fprintf(debug,"WPOL: dHH=(%10g,%10g,%10g)\n",dHH[XX],dHH[YY],dHH[ZZ]);
	fprintf(debug,"WPOL: dOD=(%10g,%10g,%10g), 1/r_OD = %10g\n",
		dOD[XX],dOD[YY],dOD[ZZ],1/r_OD);
	fprintf(debug,"WPOL: nW =(%10g,%10g,%10g), 1/r_nW = %10g\n",
		nW[XX],nW[YY],nW[ZZ],1/r_nW);
	fprintf(debug,"WPOL: dx  =%10g, dy  =%10g, dz  =%10g\n",
		dx[XX],dx[YY],dx[ZZ]);
 	fprintf(debug,"WPOL: dDSx=%10g, dDSy=%10g, dDSz=%10g\n",
		dDS[XX],dDS[YY],dDS[ZZ]);
      }
#endif
      /* Now compute the forces and energy */
      kdx[XX] = kk[XX]*dx[XX];
      kdx[YY] = kk[YY]*dx[YY];
      kdx[ZZ] = kk[ZZ]*dx[ZZ];
      vtot   += iprod(dx,kdx);
      for(m=0; (m<DIM); m++) {
	/* This is a tensor operation but written out for speed */
	tx        =  nW[m]*kdx[XX];
	ty        = dHH[m]*kdx[YY];
	tz        = dOD[m]*kdx[ZZ];
	fij       = -tx-ty-tz;
#ifdef DEBUG
	df[m] = fij;
#endif
	f[aS][m] += fij;
	f[aD][m] -= fij;
      }
#ifdef DEBUG
      if (debug) {
	fprintf(debug,"WPOL: vwpol=%g\n",0.5*iprod(dx,kdx));
	fprintf(debug,"WPOL: df = (%10g, %10g, %10g)\n",df[XX],df[YY],df[ZZ]);
      }
#endif
    }	
  }
  return 0.5*vtot;
}

real bond_angle(FILE *log,matrix box,
		rvec xi,rvec xj,rvec xk,	/* in  */
		rvec r_ij,rvec r_kj,real *costh)		/* out */
/* Return value is the angle between the bonds i-j and j-k */
{
  /* 41 FLOPS */
  real th;
  
  pbc_rvec_sub(box,xi,xj,r_ij);			/*  3		*/
  pbc_rvec_sub(box,xk,xj,r_kj);			/*  3		*/

  *costh=cos_angle(r_ij,r_kj);		/* 25		*/
  th=acos(*costh);			/* 10		*/
					/* 41 TOTAL	*/
  return th;
}

real angles(FILE *log,int nbonds,
	    t_iatom forceatoms[],t_iparams forceparams[],
	    rvec x[],rvec f[],t_forcerec *fr,t_graph *g,
	    matrix box,real lambda,real *dvdlambda,
	    t_mdatoms *md,int ngrp,real egnb[],real egcoul[])
{
  int  i,ai,aj,ak,t,type;
  rvec r_ij,r_kj;
  real cos_theta,theta,dVdt,va,vtot;
  
  vtot = 0.0;
  for(i=0; (i<nbonds); ) {
    type = forceatoms[i++];
    ai   = forceatoms[i++];
    aj   = forceatoms[i++];
    ak   = forceatoms[i++];
    
    theta  = bond_angle(log,box,x[ai],x[aj],x[ak],
			r_ij,r_kj,&cos_theta);	/*  41		*/
  
    *dvdlambda += harmonic(forceparams[type].harmonic.krA,
			   forceparams[type].harmonic.krB,
			   forceparams[type].harmonic.rA*DEG2RAD,
			   forceparams[type].harmonic.rB*DEG2RAD,
			   theta,lambda,&va,&dVdt);
    vtot += va;
    
    {
      int  m;
      real snt,st,sth;
      real cik,cii,ckk;
      real nrkj2,nrij2;
      rvec f_i,f_j,f_k;
      
      snt=sin(theta);				/*  10		*/
      if (fabs(snt) < 1e-12)
	snt=1e-12;
      st  = dVdt/snt;				/*  11		*/
      sth = st*cos_theta;				/*   1		*/
#ifdef DEBUG
      if (debug)
	fprintf(debug,"ANGLES: theta = %10g  vth = %10g  dV/dtheta = %10g\n",
		theta*RAD2DEG,va,dVdt);
#endif
      nrkj2=iprod(r_kj,r_kj);			/*   5		*/
      nrij2=iprod(r_ij,r_ij);
      
      cik=st/sqrt(nrkj2*nrij2);			/*  21		*/ 
      cii=sth/nrij2;				/*  10		*/
      ckk=sth/nrkj2;				/*  10		*/
      
      for (m=0; (m<DIM); m++) {			/*  39		*/
	f_i[m]=-(cik*r_kj[m]-cii*r_ij[m]);
	f_k[m]=-(cik*r_ij[m]-ckk*r_kj[m]);
	f_j[m]=-f_i[m]-f_k[m];
	f[ai][m]+=f_i[m];
	f[aj][m]+=f_j[m];
	f[ak][m]+=f_k[m];
      }
      t=SHIFT_INDEX(g,ai);
      rvec_inc(fr->fshift[t],f_i);
      t=SHIFT_INDEX(g,aj);
      rvec_inc(fr->fshift[t],f_j);
      t=SHIFT_INDEX(g,ak);
      rvec_inc(fr->fshift[t],f_k);
      /* 163 TOTAL	*/
    }
  }
  return vtot;
}

real dih_angle(FILE *log,matrix box,
	       rvec xi,rvec xj,rvec xk,rvec xl,
	       rvec r_ij,rvec r_kj,rvec r_kl,rvec m,rvec n,
	       real *cos_phi,real *sign)
{
  real ipr,phi;

  pbc_rvec_sub(box,xi,xj,r_ij); 		/*  3 		*/
  pbc_rvec_sub(box,xk,xj,r_kj);			/*  3		*/
  pbc_rvec_sub(box,xk,xl,r_kl);			/*  3		*/

  oprod(r_ij,r_kj,m); 			/*  9 		*/
  oprod(r_kj,r_kl,n);			/*  9		*/
  *cos_phi=cos_angle(m,n); 		/* 41 		*/
  phi=acos(*cos_phi); 			/* 10 		*/
  ipr=iprod(r_ij,n); 			/*  5 		*/
  (*sign)=(ipr<0.0)?-1.0:1.0;
  phi=(*sign)*phi; 			/*  1		*/
					/* 84 TOTAL	*/
  return phi;
}

void do_dih_fup(FILE *log,int i,int j,int k,int l,real ddphi,
		rvec r_ij,rvec r_kj,rvec r_kl,
		rvec m,rvec n,rvec f[],t_forcerec *fr,t_graph *g,
		rvec x[])
{
  /* 143 FLOPS */
  rvec f_i,f_j,f_k,f_l;
  rvec uvec,vvec,svec;
  real ipr,nrkj,nrkj2;
  real a,p,q;
  int  t;
  
  ipr=iprod(m,m);		/*  5 	*/
  nrkj2=iprod(r_kj,r_kj);	/*  5	*/
  nrkj=sqrt(nrkj2);		/* 10	*/
  a=-(ddphi*nrkj)/ipr;		/* 12	*/
  svmul(a,m,f_i);		/*  3	*/
  ipr=iprod(n,n);		/*  5	*/
  a=(ddphi*nrkj)/ipr;		/* 12	*/
  svmul(a,n,f_l);		/*  3 	*/
  p=iprod(r_ij,r_kj);		/*  5	*/
  p/=nrkj2;			/* 10	*/
  q=-iprod(r_kl,r_kj);		/*  6	*/
  q/=nrkj2;			/* 10	*/
  svmul(p,f_i,uvec);		/*  3	*/
  svmul(q,f_l,vvec);		/*  3	*/
  rvec_add(uvec,vvec,svec);	/*  3	*/
  rvec_sub(svec,f_i,f_j);	/*  3	*/
  rvec_add(svec,f_l,f_k);	/*  3	*/
  svmul(-1.0,f_k,f_k);		/*  3	*/
  rvec_add(f[i],f_i,f[i]);	/*  3	*/
  rvec_add(f[j],f_j,f[j]);	/*  3	*/
  rvec_add(f[k],f_k,f[k]);	/*  3	*/
  rvec_add(f[l],f_l,f[l]);	/*  3	*/

  t=SHIFT_INDEX(g,i);
  rvec_add(f_i,fr->fshift[t],fr->fshift[t]);
  t=SHIFT_INDEX(g,j);
  rvec_add(f_j,fr->fshift[t],fr->fshift[t]);
  t=SHIFT_INDEX(g,k);
  rvec_add(f_k,fr->fshift[t],fr->fshift[t]);
  t=SHIFT_INDEX(g,l);
  rvec_add(f_l,fr->fshift[t],fr->fshift[t]);
  /* 118 TOTAL 	*/
}


real dopdihs(real cpA,real cpB,real phiA,real phiB,int mult,
	     real phi,real lambda,real *V,real *F)
{
  real v,dvdl,mdphi,v1,sdphi,ddphi;
  real L1   = 1.0-lambda;
  real ph0  = DEG2RAD*(L1*phiA+lambda*phiB);
  real cp   = L1*cpA + lambda*cpB;
  
  mdphi =  mult*phi-ph0;
  sdphi = sin(mdphi);
  ddphi = -cp*mult*sdphi;
  v1    = 1.0+cos(mdphi);
  v     = cp*v1;
  
  dvdl  = (cpB-cpA)*v1 - cp*(phiA-phiB)*sdphi;
  
  *V = v;
  *F = ddphi;
  
  return dvdl;
  
  /* That was 40 flops */
}

real pdihs(FILE *log,int nbonds,
	   t_iatom forceatoms[],t_iparams forceparams[],
	   rvec x[],rvec f[],t_forcerec *fr,t_graph *g,
	   matrix box,real lambda,real *dvdlambda,
	   t_mdatoms *md,int ngrp,real egnb[],real egcoul[])
{
  int  i,type,ai,aj,ak,al;
  rvec r_ij,r_kj,r_kl,m,n;
  real phi,cos_phi,sign,ddphi,vpd,vtot;

  vtot = 0.0;
  for(i=0; (i<nbonds); ) {
    type = forceatoms[i++];
    ai   = forceatoms[i++];
    aj   = forceatoms[i++];
    ak   = forceatoms[i++];
    al   = forceatoms[i++];
    
    phi=dih_angle(log,box,x[ai],x[aj],x[ak],x[al],r_ij,r_kj,r_kl,m,n,
		  &cos_phi,&sign);			/*  84 		*/
		
    *dvdlambda += dopdihs(forceparams[type].pdihs.cpA,
			  forceparams[type].pdihs.cpB,
			  forceparams[type].pdihs.phiA,
			  forceparams[type].pdihs.phiB,
			  forceparams[type].pdihs.mult,
			  phi,lambda,&vpd,&ddphi);
		       
    vtot += vpd;
    do_dih_fup(log,ai,aj,ak,al,ddphi,r_ij,r_kj,r_kl,m,n,
	       f,fr,g,x); 				/* 118 		*/

#ifdef DEBUG
    fprintf(log,"pdih: (%d,%d,%d,%d) cp=%g, phi=%g\n",
	    ai,aj,ak,al,cos_phi,phi);
#endif
  } /* 229 TOTAL 	*/

  return vtot;
}

real idihs(FILE *log,int nbonds,
	   t_iatom forceatoms[],t_iparams forceparams[],
	   rvec x[],rvec f[],t_forcerec *fr,t_graph *g,
	   matrix box,real lambda,real *dvdlambda,
	   t_mdatoms *md,int ngrp,real egnb[],real egcoul[])
{
  int  i,type,ai,aj,ak,al;
  real phi,cos_phi,ddphi,sign,vid,vtot;
  rvec r_ij,r_kj,r_kl,m,n;
  
  vtot = 0.0;
  for(i=0; (i<nbonds); ) {
    type = forceatoms[i++];
    ai   = forceatoms[i++];
    aj   = forceatoms[i++];
    ak   = forceatoms[i++];
    al   = forceatoms[i++];
    
    phi=dih_angle(log,box,x[ai],x[aj],x[ak],x[al],r_ij,r_kj,r_kl,m,n,
		  &cos_phi,&sign);			/*  84		*/
    
    *dvdlambda += harmonic(forceparams[type].harmonic.krA,
			   forceparams[type].harmonic.krB,
			   forceparams[type].harmonic.rA*DEG2RAD,
			   forceparams[type].harmonic.rB*DEG2RAD,
			   phi,lambda,&vid,&ddphi);

    vtot += vid;
    do_dih_fup(log,ai,aj,ak,al,(real)(-ddphi),r_ij,r_kj,r_kl,m,n,
	       f,fr,g,x);				/* 118		*/
    /* 208 TOTAL	*/
#ifdef DEBUG
    fprintf(log,"idih: (%d,%d,%d,%d) cp=%g, phi=%g\n",
	    ai,aj,ak,al,cos_phi,phi);
#endif
  }
  return vtot;
}

real posres(FILE *log,int nbonds,
	    t_iatom forceatoms[],t_iparams forceparams[],
	    rvec x[],rvec f[],t_forcerec *fr,t_graph *g,
	    matrix box,real lambda,real *dvdlambda,
	    t_mdatoms *md,int ngrp,real egnb[],real egcoul[])
{
  int  i,ai,m,type;
  real v,vtot,fi,*fc;
  rvec dx;

  vtot = 0.0;
  for(i=0; (i<nbonds); ) {
    type = forceatoms[i++];
    ai   = forceatoms[i++];
    fc   = forceparams[type].posres.fc;

    pbc_dx(box,x[ai],forceparams[type].posres.pos0,dx);
    v=0;
    for (m=0; (m<DIM); m++) {
      fi        = f[ai][m] - fc[m]*dx[m];
      v        += 0.5*fc[m]*dx[m]*dx[m];
      f[ai][m]  = fi;
    }
    vtot += v;
  }
  return vtot;
}

real unimplemented(FILE *log,int nbonds,
		   t_iatom forceatoms[],t_iparams forceparams[],
		   rvec x[],rvec f[],t_forcerec *fr,t_graph *g,
		   matrix box,real lambda,real *dvdlambda,
		   t_mdatoms *md,int ngrp,real egnb[],real egcoul[])
{
  static char *err="*** you are using a not implemented function";

  fprintf(log,"%s\n",err);
  fprintf(stderr,"%s\n",err);
  exit(1);

  return 0.0; /* To make the compiler happy */
}

static void my_fatal(char *s,int ai,int aj,int ak,int al)
{
  fprintf(stderr,"%s is NaN in rbdih, ai-ak=%d,%d,%d,%d",s,ai,aj,ak,al);
  fflush(stderr);
  exit(1);
}

#define CHECK(s) if ((s != s) || (s < -1e10) || (s > 1e10)) my_fatal(#s,ai,aj,ak,al)

real rbdihs(FILE *log,int nbonds,
	    t_iatom forceatoms[],t_iparams forceparams[],
	    rvec x[],rvec f[],t_forcerec *fr,t_graph *g,
	    matrix box,real lambda,real *dvdlambda,
	    t_mdatoms *md,int ngrp,real egnb[],real egcoul[])
{
  static const real c0=0.0,c1=1.0,c2=2.0,c3=3.0,c4=4.0,c5=5.0;
  int  type,ai,aj,ak,al,i,j;
  rvec r_ij,r_kj,r_kl,m,n;
  real parm[NR_RBDIHS];
  real phi,cos_phi,rbp;
  real v,sign,ddphi,sin_phi;
  real cosfac,vtot;

  vtot = 0.0;
  for(i=0; (i<nbonds); ) {
    type = forceatoms[i++];
    ai   = forceatoms[i++];
    aj   = forceatoms[i++];
    ak   = forceatoms[i++];
    al   = forceatoms[i++];

    phi=dih_angle(log,box,x[ai],x[aj],x[ak],x[al],r_ij,r_kj,r_kl,m,n,
		  &cos_phi,&sign);			/*  84		*/

    /* Change to polymer convention */
    if (phi < c0)
      phi += M_PI;
    else
      phi -= M_PI;			/*   1		*/
    cos_phi = -cos_phi;					/*   1		*/
    
    sin_phi=sin(phi);
    
    for(j=0; (j<NR_RBDIHS); j++)
      parm[j] = forceparams[type].rbdihs.rbc[j];
    
    /* Calculate cosine powers */
    /* Calculate the energy */
    /* Calculate the derivative */
    v       = parm[0];
    ddphi   = c0;
    cosfac  = c1;
    
    rbp     = parm[1];
    ddphi  += rbp*cosfac;
    cosfac *= cos_phi;
    v      += cosfac*rbp;
    rbp     = parm[2];
    ddphi  += c2*rbp*cosfac;
    cosfac *= cos_phi;
    v      += cosfac*rbp;
    rbp     = parm[3];
    ddphi  += c3*rbp*cosfac;
    cosfac *= cos_phi;
    v      += cosfac*rbp;
    rbp     = parm[4];
    ddphi  += c4*rbp*cosfac;
    cosfac *= cos_phi;
    v      += cosfac*rbp;
    rbp     = parm[5];
    ddphi  += c5*rbp*cosfac;
    cosfac *= cos_phi;
    v      += cosfac*rbp;
    
    ddphi = -ddphi*sin_phi;				/*  11		*/
    
    do_dih_fup(log,ai,aj,ak,al,ddphi,r_ij,r_kj,r_kl,m,n,
	       f,fr,g,x);				/* 118		*/
    vtot += v;
  }  
  return vtot;
}

static void do_one14(rvec x[],int ai,int aj,rvec f[],int gid,
		     rvec shift,real egcoul[],real egnb[],
		     real chargeA[],real chargeB[],
		     int  typeA[],int typeB[],bool bPerturbed[],
		     real c6A,real c12A,real c6B,real c12B,
		     real r2,t_graph *g,rvec fshift[],
		     real lambda,real *dvdlambda,
		     real eps,matrix box,
		     int ntab,real tabscale,real VFtab[])
{
  real      nbfpA[100],nbfpB[100];
  rvec      f_ip,r_i;
  real      vnb,vijcoul;
  int       tA,tB;
  int       m,si,sj;
  
  clear_rvec(f_ip);

  tA = 2*typeA[aj];
  nbfpA[tA]   = c6A;
  nbfpA[tA+1] = c12A;
  
  for(m=0; (m<DIM); m++)
    r_i[m]=x[ai][m]+shift[m];
  
  vijcoul = 0.0;
  vnb     = 0.0;
  if (bPerturbed[ai] || bPerturbed[aj]) {
    tB = 2*typeB[aj];
    nbfpB[tB]   = c6B;
    nbfpB[tB+1] = c12B;
  
    c_free(r_i[XX],r_i[YY],r_i[ZZ],ai,
	   x[0],1,&aj,typeA,typeB,eps,chargeA,chargeB,
	   nbfpA,nbfpB,f[0],f_ip,&vijcoul,&vnb,
	   lambda,dvdlambda,ntab,tabscale,VFtab);
  }
  else {
    c_tab(r_i[XX],r_i[YY],r_i[ZZ],(real)(eps*chargeA[ai]),
	  x[0],1,typeA,&aj,chargeA,nbfpA,f[0],f_ip,
	  &vijcoul,&vnb,ntab,tabscale,VFtab);
  }

  egcoul[gid]    = egcoul[gid]+vijcoul;
  egnb[gid]      = egnb[gid]+vnb;

  rvec_inc(f[ai],f_ip);  
  si = SHIFT_INDEX(g,ai);
  rvec_inc(fshift[si],f_ip);
  sj = SHIFT_INDEX(g,aj);
  rvec_dec(fshift[sj],f_ip);
}

static real dist2(rvec x,rvec y)
{
  rvec dx;
  
  rvec_sub(x,y,dx);
  
  return iprod(dx,dx);
}

real do_14(FILE *log,int nbonds,t_iatom iatoms[],t_iparams *iparams,
	   rvec x[],rvec f[],t_forcerec *fr,t_graph *g,
	   matrix box,real lambda,real *dvdlambda,
	   t_mdatoms *md,int ngrp,real egnb[],real egcoul[])
{
  real      eps;
  real      r2,rlong2;
  atom_id   ai,aj,itype;
  t_iatom   *ia0,*iatom;
  int       gid;
  t_ishift  shift;
  real      c6A,c12A,c6B,c12B;

  shift = CENTRAL;
  
  /* Reaction field stuff */  
  eps    = fr->epsfac*fr->fudgeQQ;
  
  rlong2 = fr->rlong*fr->rlong;
    
  ia0=iatoms;

  for(iatom=ia0; (iatom<ia0+nbonds); iatom+=3) {
    itype = iatom[0];
    ai    = iatom[1];
    aj    = iatom[2];

    r2    = dist2(x[ai],x[aj]);
    
    if ((rlong2 > 0) && (r2 >= rlong2)) {
      fprintf(log,"%d %8.3f %8.3f %8.3f\n",(int)ai+1,
	      x[ai][XX],x[ai][YY],x[ai][ZZ]);
      fprintf(log,"%d %8.3f %8.3f %8.3f\n",(int)aj+1,
	      x[aj][XX],x[aj][YY],x[aj][ZZ]);
      fprintf(log,"1-4 (%d,%d) interaction not within cut-off! r=%g\n",
	      (int)ai+1,(int)aj+1,sqrt(r2));
      /* exit(1); */
    }
    
    gid  = GID(md->cENER[ai],md->cENER[aj],ngrp);
#ifdef DEBUG
    fprintf(log,"LJ14: grp-i=%2d, grp-j=%2d, ngrp=%2d, GID=%d\n",
	    md->cENER[ai],md->cENER[aj],ngrp,gid);
#endif
    c6A  = iparams[itype].lj14.c6A;
    c12A = iparams[itype].lj14.c12A;
    c6B  = iparams[itype].lj14.c6B;
    c12B = iparams[itype].lj14.c12B;
    
    do_one14(x,ai,aj,f,gid,fr->shift_vec[shift],egcoul,egnb,
	     md->chargeA,md->chargeB,md->typeA,md->typeB,md->bPerturbed,
	     c6A,c12A,c6B,c12B,r2,g,fr->fshift,lambda,dvdlambda,
	     eps,box,fr->ntab,fr->tabscale,fr->VFtab);
  }
  return 0.0;
}

/***********************************************************
 *
 *   G R O M O S  9 6   F U N C T I O N S
 *
 ***********************************************************/
real g96harmonic(real kA,real kB,real xA,real xB,real x,real lambda,
		 real *V,real *F)
{
  const real half=0.5;
  real  L1,kk,x0,dx,dx2;
  real  v,f,dvdl;
  
  L1    = 1.0-lambda;
  kk    = L1*kA+lambda*kB;
  x0    = L1*xA+lambda*xB;
  
  dx    = x-x0;
  dx2   = dx*dx;
  
  f     = -kk*dx;
  v     = half*kk*dx2;
  dvdl  = half*(kB-kA)*dx2 + (xA-xB)*kk*dx;
  
  *F    = f;
  *V    = v;
  
  return dvdl;
  
  /* That was 21 flops */
}

real g96bonds(FILE *log,int nbonds,
	      t_iatom forceatoms[],t_iparams forceparams[],
	      rvec x[],rvec f[],t_forcerec *fr,t_graph *g,
	      matrix box,real lambda,real *dvdlambda,
	      t_mdatoms *md,int ngrp,real egnb[],real egcoul[])
{
  int  i,m,ki,kj,ai,aj,type;
  real dr2,fbond,vbond,fij,vtot;
  rvec dx;

  vtot = 0.0;
  for(i=0; (i<nbonds); ) {
    type = forceatoms[i++];
    ai   = forceatoms[i++];
    aj   = forceatoms[i++];
  
    pbc_rvec_sub(box,x[ai],x[aj],dx);		/*   3 		*/
    dr2=iprod(dx,dx);				/*   5		*/
      
    *dvdlambda += g96harmonic(forceparams[type].harmonic.krA,
			      forceparams[type].harmonic.krB,
			      forceparams[type].harmonic.rA,
			      forceparams[type].harmonic.rB,
			      dr2,lambda,&vbond,&fbond);

    vtot  += vbond;                             /* 1*/
    fbond *= 2.0;
#ifdef DEBUG
    if (debug)
      fprintf(debug,"G96-BONDS: dr = %10g  vbond = %10g  fbond = %10g\n",
	      sqrt(dr2),vbond,fbond);
#endif
    ki=SHIFT_INDEX(g,ai);
    kj=SHIFT_INDEX(g,aj);
    for (m=0; (m<DIM); m++) {			/*  15		*/
      fij=fbond*dx[m];
      f[ai][m]+=fij;
      f[aj][m]-=fij;
      fr->fshift[ki][m]+=fij;
      fr->fshift[kj][m]-=fij;
    }
  }					/* 44 TOTAL	*/
  return vtot;
}

real g96bond_angle(FILE *log,matrix box,
		   rvec xi,rvec xj,rvec xk,	/* in  */
		   rvec r_ij,rvec r_kj)		/* out */
/* Return value is the angle between the bonds i-j and j-k */
{
  real costh;
  
  pbc_rvec_sub(box,xi,xj,r_ij);			/*  3		*/
  pbc_rvec_sub(box,xk,xj,r_kj);			/*  3		*/

  costh=cos_angle(r_ij,r_kj);		/* 25		*/
					/* 41 TOTAL	*/
  return costh;
}

real g96angles(FILE *log,int nbonds,
	       t_iatom forceatoms[],t_iparams forceparams[],
	       rvec x[],rvec f[],t_forcerec *fr,t_graph *g,
	       matrix box,real lambda,real *dvdlambda,
	       t_mdatoms *md,int ngrp,real egnb[],real egcoul[])
{
  int  i,ai,aj,ak,t,type,m;
  rvec r_ij,r_kj;
  real cos_theta,dVdt,va,vtot;
  real rij_1,rij_2,rkj_1,rkj_2,rijrkj_1;
  rvec f_i,f_j,f_k;
  
  vtot = 0.0;
  for(i=0; (i<nbonds); ) {
    type = forceatoms[i++];
    ai   = forceatoms[i++];
    aj   = forceatoms[i++];
    ak   = forceatoms[i++];
    
    cos_theta  = g96bond_angle(log,box,x[ai],x[aj],x[ak],r_ij,r_kj);	
    
    *dvdlambda += g96harmonic(forceparams[type].harmonic.krA,
			      forceparams[type].harmonic.krB,
			      forceparams[type].harmonic.rA,
			      forceparams[type].harmonic.rB,
			      cos_theta,lambda,&va,&dVdt);
    vtot    += va;
    
    rij_1    = invsqrt(iprod(r_ij,r_ij));
    rkj_1    = invsqrt(iprod(r_kj,r_kj));
    rij_2    = rij_1*rij_1;
    rkj_2    = rkj_1*rkj_1;
    rijrkj_1 = rij_1*rkj_1;                     /* 23 */
    
#ifdef DEBUG
    if (debug)
      fprintf(debug,"G96ANGLES: costheta = %10g  vth = %10g  dV/dct = %10g\n",
	      cos_theta,va,dVdt);
#endif
    for (m=0; (m<DIM); m++) {			/*  42	*/
      f_i[m]=dVdt*(r_kj[m]*rijrkj_1 - r_ij[m]*rij_2*cos_theta);
      f_k[m]=dVdt*(r_ij[m]*rijrkj_1 - r_kj[m]*rkj_2*cos_theta);
      f_j[m]=-f_i[m]-f_k[m];
      f[ai][m]+=f_i[m];
      f[aj][m]+=f_j[m];
      f[ak][m]+=f_k[m];
    }
    t = SHIFT_INDEX(g,ai);
    rvec_inc(fr->fshift[t],f_i);
    t = SHIFT_INDEX(g,aj);
    rvec_inc(fr->fshift[t],f_j);
    t = SHIFT_INDEX(g,ak);
    rvec_inc(fr->fshift[t],f_k);               /* 9 */
    /* 163 TOTAL	*/
  }
  return vtot;
}

