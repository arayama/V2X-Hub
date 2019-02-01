/*
 * Generated by asn1c-0.9.29 (http://lionet.info/asn1c)
 * From ASN.1 module "AddGrpC"
 * 	found in "../J2735_201603DA.ASN"
 * 	`asn1c -gen-PER -fcompound-names -fincludes-quoted -S/home/gmb/TMX-OAM/Build/asn1c/skeletons`
 */

#include "SignalHeadLocationList.h"

asn_per_constraints_t asn_PER_type_SignalHeadLocationList_constr_1 GCC_NOTUSED = {
	{ APC_UNCONSTRAINED,	-1, -1,  0,  0 },
	{ APC_CONSTRAINED,	 5,  5,  1,  20 }	/* (SIZE(1..20)) */,
	0, 0	/* No PER value map */
};
asn_TYPE_member_t asn_MBR_SignalHeadLocationList_1[] = {
	{ ATF_POINTER, 0, 0,
		(ASN_TAG_CLASS_UNIVERSAL | (16 << 2)),
		0,
		&asn_DEF_SignalHeadLocation,
		0,
		0,	/* Defer constraints checking to the member type */
		0,	/* OER is not compiled, use -gen-OER */
		0,	/* No PER visible constraints */
		0,
		""
		},
};
static const ber_tlv_tag_t asn_DEF_SignalHeadLocationList_tags_1[] = {
	(ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
asn_SET_OF_specifics_t asn_SPC_SignalHeadLocationList_specs_1 = {
	sizeof(struct SignalHeadLocationList),
	offsetof(struct SignalHeadLocationList, _asn_ctx),
	0,	/* XER encoding is XMLDelimitedItemList */
};
asn_TYPE_descriptor_t asn_DEF_SignalHeadLocationList = {
	"SignalHeadLocationList",
	"SignalHeadLocationList",
	&asn_OP_SEQUENCE_OF,
	SEQUENCE_OF_constraint,
	asn_DEF_SignalHeadLocationList_tags_1,
	sizeof(asn_DEF_SignalHeadLocationList_tags_1)
		/sizeof(asn_DEF_SignalHeadLocationList_tags_1[0]), /* 1 */
	asn_DEF_SignalHeadLocationList_tags_1,	/* Same as above */
	sizeof(asn_DEF_SignalHeadLocationList_tags_1)
		/sizeof(asn_DEF_SignalHeadLocationList_tags_1[0]), /* 1 */
	0,	/* No OER visible constraints */
	&asn_PER_type_SignalHeadLocationList_constr_1,
	asn_MBR_SignalHeadLocationList_1,
	1,	/* Single element */
	&asn_SPC_SignalHeadLocationList_specs_1	/* Additional specs */
};

