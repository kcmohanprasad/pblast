#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mysql.h>

#include "misc.h"

MYSQL *mysql;

int database_connect( char *database)
{
    int ret = 0;
	char query[200];

	mysql = mysql_init(NULL);
    if (mysql == NULL) {
        fprintf(stderr, "Failed to init database: Error: %s\n", mysql_error(mysql));
		ret = 1;
	}

    if (!mysql_real_connect(mysql,"localhost","root","test123",database,0,NULL,0)) {
        fprintf(stderr, "Failed to connect to database: Error: %s\n", mysql_error(mysql));
		ret = 1;
	}

	// Clear all the rows in Ports table
	sprintf(query,"truncate portinfo");
	if (mysql_query(mysql, query)) {
        fprintf(stderr, "Failed to clear database: Error: %s\n", mysql_error(mysql));
		ret = 1;
	}
    return ret;
}

int insert_table(int port, char *port_name, int status)
{
	char query[200];
	sprintf(query,"INSERT INTO portinfo VALUES (%d, %d, '%s')", port, status, port_name);
	if (mysql_query(mysql, query)) {
        fprintf(stderr, "Failed to update database: Error: %s\n", mysql_error(mysql));
	}
	return 0;
}


int update_table(int port, int status)
{
	char query[200];
	sprintf(query,"update portinfo set Status=%d where PortNumber=%d",status,port);
	if (mysql_query(mysql, query)) {
        fprintf(stderr, "Failed to update database: Error: %s\n", mysql_error(mysql));
	}
	return 0;
}

/*
    MYSQL_RES *result;
    MYSQL_ROW *row;
    MYSQL_FIELD *field;

    char stat[50] = "INSERT INTO Users VALUES('%s')";
    char updatename[200] = "UPDATE Users SET name='%s' WHERE name='%s'";
    scanf("%d",&choice);
    switch(choice) {
		case 1:
		{
			insert code
		}
		case 2:
		{
			display code
		}
		case 3:
		{
			printf("edit name");
                    scanf("%s",search);
                    mysql_query(conn,"SELECT * FROM Users");
                    result=mysql_store_result(conn);
                    num_fields=mysql_num_fields(result);
                    while((row=mysql_fetch_row(result)))
                    {
                            for(i=0;i<num_fields;i++)
                            {
                                    if(strcmp(search,row[i])==0)
                                    {
                                            //printf("%s",row[i]);
                                            printf("Enter new name\n");
                                            scanf("%s",ename);
                                            len=snprintf(query,sizeof(updatename)+strlen(ename),updatename,ename);
                                            //printf("%d",len);
                                            printf("%s\n",query);
                                            mysql_query(conn,query);
                                            mysql_close(conn);
                                    }
                            }
			}
		break;
		}
	}
}

*/
