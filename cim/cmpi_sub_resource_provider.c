/*
 * cmpi_sub_resource_provider.c: LinuxHA_SubResource provider
 * 
 * Author: Jia Ming Pan <jmltc@cn.ibm.com>
 * Copyright (c) 2005 International Business Machines
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <portability.h>
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <hb_api.h>
#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>
#include "cmpi_utils.h"
#include "linuxha_info.h"
#include "cmpi_sub_resource.h"


#define PROVIDER_ID "cim-sub-res"

static CMPIBroker * Broker         = NULL;
static char ClassName []           = "LinuxHA_SubResource"; 

static char group_ref []           = "Group";
static char group_class_name []    = "LinuxHA_ClusterResourceGroup"; 
static char resource_ref []        = "Resource"; 
static char resource_class_name [] = "LinuxHA_ClusterResource";


/***************** instance interfaces *******************/
CMPIStatus SubResource_Cleanup(CMPIInstanceMI * mi, CMPIContext * ctx);
CMPIStatus SubResource_EnumInstanceNames(CMPIInstanceMI * mi,
                CMPIContext * ctx, CMPIResult * rslt, CMPIObjectPath * ref);
CMPIStatus 
SubResource_EnumInstances(CMPIInstanceMI * mi, CMPIContext * ctx, 
                CMPIResult * rslt, CMPIObjectPath * ref, char ** properties);
CMPIStatus 
SubResource_GetInstance(CMPIInstanceMI * mi, CMPIContext * ctx, 
                CMPIResult * rslt, CMPIObjectPath * cop, char ** properties);
CMPIStatus 
SubResource_CreateInstance(CMPIInstanceMI * mi, CMPIContext * ctx, 
                CMPIResult * rslt, CMPIObjectPath * cop, CMPIInstance * ci);
CMPIStatus 
SubResource_SetInstance(CMPIInstanceMI * mi, CMPIContext * ctx, 
                CMPIResult * rslt, CMPIObjectPath * cop, CMPIInstance * ci, 
                char ** properties);
CMPIStatus 
SubResource_DeleteInstance(CMPIInstanceMI * mi,
                CMPIContext * ctx, CMPIResult * rslt, CMPIObjectPath * cop);

CMPIStatus 
SubResource_ExecQuery(CMPIInstanceMI * mi,
                CMPIContext * ctx, CMPIResult * rslt,
                CMPIObjectPath * ref, char * lang, char * query);

/*********************** association interfaces ***********************/
CMPIStatus 
SubResource_AssociationCleanup(CMPIAssociationMI * mi, 
                               CMPIContext * ctx);
CMPIStatus
SubResource_Associators(CMPIAssociationMI * mi, CMPIContext * ctx, 
                CMPIResult * rslt, CMPIObjectPath * op, 
                const char * asscClass, const char * resultClass,
                const char * role, const char * resultRole, 
                char ** properties);
CMPIStatus
SubResource_AssociatorNames(CMPIAssociationMI * mi, CMPIContext * ctx, 
                CMPIResult * rslt, CMPIObjectPath * op, 
                const char * asscClass, const char * resultClass,
                const char * role, const char * resultRole);
CMPIStatus
SubResource_References(CMPIAssociationMI * mi, CMPIContext * ctx, CMPIResult * rslt,
                CMPIObjectPath * op, const char * resultClass,
                const char * role, char ** properties);
CMPIStatus
SubResource_ReferenceNames(CMPIAssociationMI * mi,
                CMPIContext * ctx, CMPIResult * rslt, CMPIObjectPath * cop,
                const char * assocClass, const char * role);

/**********************************************
 * Instance 
 **********************************************/
CMPIStatus 
SubResource_Cleanup(CMPIInstanceMI * mi, CMPIContext * ctx)
{
        subres_inst_cleanup(mi, ctx);
        CMReturn(CMPI_RC_OK);
}


CMPIStatus 
SubResource_EnumInstanceNames(CMPIInstanceMI * mi, CMPIContext* ctx, 
                              CMPIResult * rslt, CMPIObjectPath * ref)
{
        CMPIStatus rc = {CMPI_RC_OK, NULL};
        int ret = 0;

        init_logger(PROVIDER_ID);

        DEBUG_ENTER();
        ret = enum_inst_assoc(Broker, ClassName, resource_ref, group_ref, 
                              resource_class_name, group_class_name,
                              ctx, rslt, ref, &is_sub_resource_of, 0, &rc);

        DEBUG_LEAVE();
        if ( ret != HA_OK ){
                return rc;
        }
        CMReturn(CMPI_RC_OK);
}


CMPIStatus 
SubResource_EnumInstances(CMPIInstanceMI * mi,
                          CMPIContext * ctx, CMPIResult * rslt,
                          CMPIObjectPath * ref, char ** properties)
{
        CMPIStatus rc = {CMPI_RC_OK, NULL};
        int ret = 0;

        init_logger(PROVIDER_ID);
        DEBUG_ENTER();
        ret = enum_inst_assoc(Broker, ClassName, resource_ref, group_ref, 
                              resource_class_name, group_class_name,
                              ctx, rslt, ref, &is_sub_resource_of, 1, &rc);

        DEBUG_LEAVE();
        if ( ret != HA_OK ){
                return rc;
        }

        CMReturn(CMPI_RC_OK);
}


CMPIStatus 
SubResource_GetInstance(CMPIInstanceMI * mi, CMPIContext * ctx, CMPIResult * rslt,
                        CMPIObjectPath * cop, char ** properties)
{

        CMPIStatus rc = {CMPI_RC_OK, NULL};
        int ret = 0;

        init_logger(PROVIDER_ID);

        DEBUG_ENTER();
        ret = get_inst_assoc(Broker, ClassName, group_ref, resource_ref, 
                             group_class_name, resource_class_name,
                             ctx, rslt, cop, &rc);

        DEBUG_LEAVE();
        if ( ret != HA_OK ){
                return rc;
        }
        CMReturn(CMPI_RC_OK);
       
}

CMPIStatus 
SubResource_CreateInstance(CMPIInstanceMI * mi, CMPIContext * ctx, 
                           CMPIResult * rslt, CMPIObjectPath * cop, 
                           CMPIInstance * ci)
{
        CMPIStatus rc = {CMPI_RC_OK, NULL};
        CMSetStatusWithChars(Broker, &rc, 
                        CMPI_RC_ERR_NOT_SUPPORTED, "CIM_ERR_NOT_SUPPORTED");
        return rc;
}


CMPIStatus 
SubResource_SetInstance(CMPIInstanceMI * mi, CMPIContext * ctx, 
                        CMPIResult * rslt, CMPIObjectPath * cop, 
                        CMPIInstance * ci, char ** properties)
{
        CMPIStatus rc = {CMPI_RC_OK, NULL};
        CMSetStatusWithChars(Broker, &rc, 
                        CMPI_RC_ERR_NOT_SUPPORTED, "CIM_ERR_NOT_SUPPORTED");
        return rc;

}


CMPIStatus 
SubResource_DeleteInstance(CMPIInstanceMI * mi, CMPIContext * ctx, 
                           CMPIResult * rslt, CMPIObjectPath * cop)
{
        CMPIStatus rc = {CMPI_RC_OK, NULL};
        CMSetStatusWithChars(Broker, &rc, 
                        CMPI_RC_ERR_NOT_SUPPORTED, "CIM_ERR_NOT_SUPPORTED");
        return rc;
}

CMPIStatus 
SubResource_ExecQuery(CMPIInstanceMI * mi, CMPIContext * ctx, 
                      CMPIResult * rslt, CMPIObjectPath *ref,
                      char * lang, char * query)
{
        CMPIStatus rc = {CMPI_RC_OK, NULL};
        CMSetStatusWithChars(Broker, &rc, 
                        CMPI_RC_ERR_NOT_SUPPORTED, "CIM_ERR_NOT_SUPPORTED");
        return rc;
}


/****************************************************
 * Association
 ****************************************************/
CMPIStatus 
SubResource_AssociationCleanup(CMPIAssociationMI * mi, CMPIContext * ctx)
{
        subres_assoc_cleanup(mi, ctx);
        CMReturn(CMPI_RC_OK);
}

CMPIStatus
SubResource_Associators(CMPIAssociationMI * mi, CMPIContext * ctx, 
                        CMPIResult * rslt,  CMPIObjectPath * cop, 
                        const char * assocClass, const char * resultClass,
                        const char * role, const char * resultRole, 
                        char ** properties)
{
        CMPIStatus rc = {CMPI_RC_OK, NULL};
        int ret = 0;

        init_logger(PROVIDER_ID);
        DEBUG_ENTER();
        ret = enum_associators(Broker, ClassName, resource_ref, group_ref,
                               resource_class_name, group_class_name, 
                               ctx, rslt, cop, assocClass,
                               resultClass, role, resultRole, 
                               &is_sub_resource_of, 1, &rc); 
        
        DEBUG_LEAVE();
        if ( ret != HA_OK ){
                return rc;
        }
        CMReturn(CMPI_RC_OK);
}


CMPIStatus
SubResource_AssociatorNames(CMPIAssociationMI * mi, CMPIContext * ctx, 
                            CMPIResult * rslt, CMPIObjectPath * cop, 
                            const char * assocClass, const char * resultClass,
                            const char * role, const char * resultRole)
{

        CMPIStatus rc = {CMPI_RC_OK, NULL};
        int ret = 0;
        
        init_logger(PROVIDER_ID);
        DEBUG_ENTER();

        ret = enum_associators(Broker, ClassName, resource_ref, group_ref,
                               resource_class_name, group_class_name, 
                               ctx, rslt, cop, assocClass,
                               resultClass, role, resultRole, 
                               &is_sub_resource_of, 0, &rc); 

        DEBUG_LEAVE();
        if ( ret != HA_OK ){
                return rc;
        }
        CMReturn(CMPI_RC_OK);
}

CMPIStatus
SubResource_References(CMPIAssociationMI * mi, CMPIContext * ctx, 
                       CMPIResult * rslt, CMPIObjectPath * cop, 
                       const char * resultClass, const char * role, 
                       char ** properties)
{
        CMPIStatus rc = {CMPI_RC_OK, NULL};
        int ret = 0;

        init_logger(PROVIDER_ID);

        DEBUG_ENTER();
        ret = enum_references(Broker, ClassName, resource_ref, group_ref,
                              resource_class_name, group_class_name,
                              ctx, rslt, cop, resultClass, role,
                              &is_sub_resource_of, 1, &rc);

        DEBUG_LEAVE();
        if ( ret != HA_OK ){
                return rc;
        }

        CMReturn(CMPI_RC_OK);

}

CMPIStatus
SubResource_ReferenceNames(CMPIAssociationMI * mi, CMPIContext * ctx, 
                           CMPIResult * rslt, CMPIObjectPath * cop,
                           const char * resultClass, const char * role)
{
        CMPIStatus rc = {CMPI_RC_OK, NULL};
        int ret = 0;
        init_logger(PROVIDER_ID);
        DEBUG_ENTER();
        ret = enum_references(Broker, ClassName, resource_ref, group_ref, 
                              resource_class_name, group_class_name,
                              ctx, rslt, cop, resultClass, role,
                              &is_sub_resource_of, 0, &rc);
        DEBUG_LEAVE();
        if ( ret != HA_OK ){
                return rc;
        }
        CMReturn(CMPI_RC_OK);

}                


DeclareInstanceMI(SubResource_, LinuxHA_SubResourceProvider, Broker);
DeclareAssociationMI(SubResource_, LinuxHA_SubResourceProvider, Broker);
