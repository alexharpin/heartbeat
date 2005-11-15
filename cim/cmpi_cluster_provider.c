/*
 * cmpi_cluster_provider.c: LinuxHA_Cluster provider
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
#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>
#include "cmpi_utils.h"
#include "cmpi_cluster.h"

#define PROVIDER_ID          "cim-cluster"
static CMPIBroker * Broker = NULL;
static char  ClassName []  = "LinuxHA_Cluster";

#define ReturnErrRC(rc)                       \
do{                                           \
        if (rc.rc == CMPI_RC_OK ) {           \
                CMReturn(CMPI_RC_ERR_FAILED); \
        } else {                              \
                return rc;                    \
        }                                     \
} while(0)

CMPIStatus 
Cluster_Cleanup(CMPIInstanceMI * mi, CMPIContext * ctx);
CMPIStatus 
Cluster_EnumInstanceNames(CMPIInstanceMI * mi, CMPIContext * ctx, 
                CMPIResult * rslt, CMPIObjectPath * ref);
CMPIStatus 
Cluster_EnumInstances(CMPIInstanceMI * mi, CMPIContext * ctx, 
                CMPIResult * rslt, CMPIObjectPath* ref, char ** properties);
CMPIStatus 
Cluster_GetInstance(CMPIInstanceMI * mi, CMPIContext * ctx, 
                CMPIResult * rslt, CMPIObjectPath * cop, char ** properties);
CMPIStatus 
Cluster_CreateInstance(CMPIInstanceMI * mi, CMPIContext * ctx, 
                CMPIResult * rslt, CMPIObjectPath * cop, CMPIInstance * ci);
CMPIStatus 
Cluster_SetInstance(CMPIInstanceMI * mi, CMPIContext * ctx, CMPIResult * rslt,
                CMPIObjectPath * op, CMPIInstance * inst, char ** properties);
CMPIStatus 
Cluster_DeleteInstance(CMPIInstanceMI * mi, CMPIContext * ctx, 
                CMPIResult * rslt, CMPIObjectPath * cop);
CMPIStatus 
Cluster_ExecQuery(CMPIInstanceMI * mi, CMPIContext * ctx, CMPIResult * rslt, 
                CMPIObjectPath * ref, char * lang, char * query);

/* method */
CMPIStatus 
Cluster_InvokeMethod(CMPIMethodMI * mi, CMPIContext * ctx, CMPIResult * rslt,
                CMPIObjectPath * ref, const char * method,
                CMPIArgs * in, CMPIArgs * out);
CMPIStatus 
Cluster_MethodCleanup(CMPIMethodMI * mi, CMPIContext * ctx);

/**********************************************
 * Instance Provider Interface
 **********************************************/

CMPIStatus 
Cluster_Cleanup(CMPIInstanceMI * mi, CMPIContext * ctx)
{

        cleanup_cluster();
        CMReturn(CMPI_RC_OK);
}

CMPIStatus 
Cluster_EnumInstanceNames(CMPIInstanceMI * mi, CMPIContext * ctx, 
                          CMPIResult * rslt, CMPIObjectPath *ref)
{
        CMPIStatus rc = {CMPI_RC_OK, NULL};
        if ( enum_cluster_instances(ClassName, Broker, ctx, rslt,
                                    ref, NULL, 0, &rc) != HA_OK ) {
                ReturnErrRC(rc);
        }

        CMReturn(CMPI_RC_OK);

}

CMPIStatus 
Cluster_EnumInstances(CMPIInstanceMI * mi, CMPIContext * ctx, CMPIResult * rslt,
                      CMPIObjectPath* ref, char ** properties)
{
        CMPIStatus rc = {CMPI_RC_OK, NULL};

        if ( enum_cluster_instances(ClassName, Broker, ctx, rslt,
                                    ref, properties, 1, &rc) == HA_OK ) {
                CMReturn(CMPI_RC_OK);                
        } else {
                ReturnErrRC(rc);
        }

}


CMPIStatus 
Cluster_GetInstance(CMPIInstanceMI * mi, CMPIContext * ctx, CMPIResult * rslt,
                    CMPIObjectPath * cop, char ** properties)
{
        CMPIStatus rc = {CMPI_RC_OK, NULL};
        init_logger(PROVIDER_ID);

        if ( get_cluster_instance(ClassName, Broker, 
                                  ctx, rslt, cop, &rc) == HA_OK ) {
                CMReturn(CMPI_RC_OK);
        } else {
                cl_log(LOG_WARNING, "%s: failed to get instance", __FUNCTION__);
                ReturnErrRC(rc);
        }

}


CMPIStatus 
Cluster_CreateInstance(CMPIInstanceMI * mi, CMPIContext * ctx, CMPIResult * rslt,
                       CMPIObjectPath * cop, CMPIInstance * ci)
{
        CMPIStatus rc = {CMPI_RC_OK, NULL};
        CMSetStatusWithChars(Broker, &rc, 
                        CMPI_RC_ERR_NOT_SUPPORTED, "CIM_ERR_NOT_SUPPORTED");
        return rc;
}


CMPIStatus 
Cluster_SetInstance(CMPIInstanceMI * mi, CMPIContext * ctx, CMPIResult * rslt,
                CMPIObjectPath * op, CMPIInstance * inst, char ** properties)
{
        CMPIStatus rc = {CMPI_RC_OK, NULL};

        CMSetStatusWithChars(Broker, &rc, 
                        CMPI_RC_ERR_NOT_SUPPORTED, "CIM_ERR_NOT_SUPPORTED");
        return rc;

}


CMPIStatus 
Cluster_DeleteInstance(CMPIInstanceMI * mi,  CMPIContext * ctx, 
                       CMPIResult * rslt, CMPIObjectPath * cop)
{
        CMPIStatus rc = {CMPI_RC_OK, NULL};

        CMSetStatusWithChars(Broker, &rc, 
                        CMPI_RC_ERR_NOT_SUPPORTED, "CIM_ERR_NOT_SUPPORTED");
        return rc;
}

CMPIStatus 
Cluster_ExecQuery(CMPIInstanceMI * mi, CMPIContext * ctx, CMPIResult * rslt, 
                  CMPIObjectPath * ref, char * lang, char * query)
{
        CMPIStatus rc = {CMPI_RC_OK, NULL};
        
        CMSetStatusWithChars(Broker, &rc, 
                        CMPI_RC_ERR_NOT_SUPPORTED, "CIM_ERR_NOT_SUPPORTED");
        return rc;
}


/**************************************************
 * Method Provider Interface
 *************************************************/
CMPIStatus 
Cluster_InvokeMethod(CMPIMethodMI * mi, CMPIContext * ctx, CMPIResult * rslt,
                CMPIObjectPath * ref, const char * method_name,
                CMPIArgs * in, CMPIArgs * out)
{

        CMPIString* class_name = NULL;
        CMPIStatus rc = {CMPI_RC_OK, NULL};
        CMPIData arg_data;
        CMPIValue valrc;
        char param [] = "action";

        init_logger(PROVIDER_ID);
        class_name = CMGetClassName(ref, &rc);

        arg_data = CMGetArg(in, param, &rc);

        if(strcasecmp(CMGetCharPtr(class_name), ClassName) == 0 &&
           strcasecmp("RequestStatusChange", method_name) == 0 ){
                cl_log(LOG_INFO, "%s: NOT IMPLEMENTED", __FUNCTION__);
        }

        CMReturnData(rslt, &valrc, CMPI_uint32);
        CMReturnDone(rslt);

        CMReturn(CMPI_RC_OK);
}


CMPIStatus 
Cluster_MethodCleanup(CMPIMethodMI* mi, CMPIContext* ctx)
{
        CMReturn(CMPI_RC_OK);
}



/*****************************************************
 * install interface
 ****************************************************/
DeclareInstanceMI(Cluster_, LinuxHA_ClusterProvider, Broker);
DeclareMethodMI(Cluster_, LinuxHA_ClusterProvider, Broker);
