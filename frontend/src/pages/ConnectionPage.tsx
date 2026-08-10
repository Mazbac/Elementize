import { Globe2, ShieldCheck } from 'lucide-react';
import { Accordion, AccordionContent, AccordionItem, AccordionTrigger } from '@/components/ui/accordion';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Table, TableBody, TableCell, TableRow } from '@/components/ui/table';
import type { ElementizeAdminConfig } from '../types';

type Props = { config: ElementizeAdminConfig };

export function ConnectionPage({ config }: Props) {
  const ready = config.environment.connectionReady;

  return (
    <div className="space-y-5">
      <div>
        <h2 className="text-lg font-semibold">Connection</h2>
        <p className="mt-1 max-w-2xl text-sm text-muted-foreground">
          Elementize needs one public HTTPS address that ChatGPT can reach. Most users set this once and leave it alone.
        </p>
      </div>

      <Card>
        <CardHeader className="pb-3">
          <div className="flex flex-col gap-3 sm:flex-row sm:items-start sm:justify-between">
            <div className="flex items-start gap-3">
              <div className="flex h-8 w-8 shrink-0 items-center justify-center rounded-md bg-secondary text-primary">
                {ready ? <ShieldCheck className="h-4 w-4" /> : <Globe2 className="h-4 w-4" />}
              </div>
              <div>
                <CardTitle>{ready ? 'Secure address ready' : 'Public address required'}</CardTitle>
                <CardDescription className="mt-1 max-w-2xl leading-5">
                  {ready
                    ? 'This is the address Elementize places into the Action schema for your Custom GPT.'
                    : 'Local WordPress sites need an HTTPS tunnel. Production sites can usually use their normal HTTPS domain.'}
                </CardDescription>
              </div>
            </div>
            <Badge variant={ready ? 'default' : 'secondary'}>{ready ? 'Ready' : 'Needs setup'}</Badge>
          </div>
        </CardHeader>
        {ready && (
          <CardContent>
            <div className="rounded-md border bg-secondary/40 px-3 py-2 font-mono text-xs break-all">{config.environment.effectiveOrigin}</div>
          </CardContent>
        )}
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Public HTTPS address</CardTitle>
          <CardDescription>Use the website origin only. Do not include <code>/wp-json</code> or another path.</CardDescription>
        </CardHeader>
        <CardContent className="space-y-4">
          <form method="post" action={config.urls.adminPost} className="space-y-3">
            <input type="hidden" name="action" value="elementize_save_connection" />
            <input type="hidden" name="_wpnonce" value={config.nonces.saveConnection} />
            <div className="space-y-1.5">
              <label htmlFor="elementize_public_api_origin" className="text-sm font-medium">Address</label>
              <Input
                id="elementize_public_api_origin"
                name="elementize_public_api_origin"
                type="url"
                defaultValue={config.environment.storedPublic}
                placeholder={config.environment.effectiveOrigin || 'https://your-secure-address.example'}
              />
              <p className="text-xs text-muted-foreground">Example: https://example.com</p>
            </div>
            <Button type="submit">Save address</Button>
          </form>

          {config.environment.storedPublic && (
            <form method="post" action={config.urls.adminPost} className="border-t pt-4">
              <input type="hidden" name="action" value="elementize_save_connection" />
              <input type="hidden" name="elementize_clear_public_origin" value="1" />
              <input type="hidden" name="_wpnonce" value={config.nonces.saveConnection} />
              <Button type="submit" variant="outline">Use automatic site address instead</Button>
            </form>
          )}
        </CardContent>
      </Card>

      <Card>
        <CardContent className="pt-2">
          <Accordion type="single" collapsible>
            <AccordionItem value="technical" className="border-0">
              <AccordionTrigger>
                <div className="text-left">
                  <p className="font-medium">Technical details</p>
                  <p className="mt-1 text-xs font-normal text-muted-foreground">Only needed when troubleshooting.</p>
                </div>
              </AccordionTrigger>
              <AccordionContent>
                <Table>
                  <TableBody>
                    <TableRow>
                      <TableCell className="w-[220px] text-muted-foreground">WordPress site</TableCell>
                      <TableCell className="font-medium break-all">{config.environment.siteOrigin || 'Unavailable'}</TableCell>
                    </TableRow>
                    <TableRow>
                      <TableCell className="text-muted-foreground">Public HTTPS detected</TableCell>
                      <TableCell className="font-medium">{config.environment.sitePublicHttps ? 'Yes' : 'No'}</TableCell>
                    </TableRow>
                    <TableRow>
                      <TableCell className="text-muted-foreground">Stored override</TableCell>
                      <TableCell className="font-medium break-all">{config.environment.storedPublic || 'None'}</TableCell>
                    </TableRow>
                  </TableBody>
                </Table>
              </AccordionContent>
            </AccordionItem>
          </Accordion>
        </CardContent>
      </Card>
    </div>
  );
}
