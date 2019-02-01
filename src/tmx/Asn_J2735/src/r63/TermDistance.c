/*
 * Generated by asn1c-0.9.29 (http://lionet.info/asn1c)
 * From ASN.1 module "DSRC"
 * 	found in "../J2735_201603DA.ASN"
 * 	`asn1c -gen-PER -fcompound-names -fincludes-quoted -S/home/gmb/TMX-OAM/Build/asn1c/skeletons`
 */

#include "TermDistance.h"

int
TermDistance_constraint(asn_TYPE_descriptor_t *td, const void *sptr,
			asn_app_constraint_failed_f *ctfailcb, void *app_key) {
	long value;
	
	if(!sptr) {
		ASN__CTFAIL(app_key, td, sptr,
			"%s: value not given (%s:%d)",
			td->name, __FILE__, __LINE__);
		return -1;
	}
	
	value = *(const long *)sptr;
	
	if((value >= 1 && value <= 30000)) {
		/* Constraint check succeeded */
		return 0;
	} else {
		ASN__CTFAIL(app_key, td, sptr,
			"%s: constraint failed (%s:%d)",
			td->name, __FILE__, __LINE__);
		return -1;
	}
}

/*
 * This type is implemented using NativeInteger,
 * so here we adjust the DEF accordingly.
 */
asn_per_constraints_t asn_PER_type_TermDistance_constr_1 GCC_NOTUSED = {
	{ APC_CONSTRAINED,	 15,  15,  1,  30000 }	/* (1..30000) */,
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	0, 0	/* No PER value map */
};
static const ber_tlv_tag_t asn_DEF_TermDistance_tags_1[] = {
	(ASN_TAG_CLASS_UNIVERSAL | (2 << 2))
};
asn_TYPE_descriptor_t asn_DEF_TermDistance = {
	"TermDistance",
	"TermDistance",
	&asn_OP_NativeInteger,
	TermDistance_constraint,
	asn_DEF_TermDistance_tags_1,
	sizeof(asn_DEF_TermDistance_tags_1)
		/sizeof(asn_DEF_TermDistance_tags_1[0]), /* 1 */
	asn_DEF_TermDistance_tags_1,	/* Same as above */
	sizeof(asn_DEF_TermDistance_tags_1)
		/sizeof(asn_DEF_TermDistance_tags_1[0]), /* 1 */
	0,	/* No OER visible constraints */
	&asn_PER_type_TermDistance_constr_1,
	0, 0,	/* No members */
	0	/* No specifics */
};

