import { Accordion, AccordionContent, AccordionItem, AccordionTrigger } from '@/components/ui/accordion';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Separator } from '@/components/ui/separator';
import { Table, TableBody, TableCell, TableRow } from '@/components/ui/table';
import type { ElementizeAdminConfig } from '../types';

type Props = { config: ElementizeAdminConfig };

export function ConnectionPage({ config }: Props) {
  const ready = config.environment.connectionReady;

  return (
    <div className="space-y-5">
      <div>
        <h2 className="text-lg font-semibold">Connection</h2>
        <p className="mt-1 text-sm text-muted-foreground">Set the public HTTPS address ChatGPT uses to reach Elementize.</p>
      </div>

      <Card>
        <CardHeader>
          <div className="flex items-start justify-between gap-4">
            <div>
              <CardTitle>Public HTTPS address</CardTitle>
              <CardDescription className="mt-1">Set this once. Use the website origin only, without <code>/wp-json</code> or another path.</CardDescription>
            </div>
            <Badge variant={ready ? 'default' : 'secondary'}>{ready ? 'Ready' : 'Needs setup'}</Badge>
          </div>
        </CardHeader>
        <CardContent className="space-y-4">
          {ready && (
            <p className="break-all text-sm text-muted-foreground">Current: <span className="font-medium text-foreground">{config.environment.effectiveOrigin}</span></p>
          )}

          <form method="post" action={config.urls.adminPost} className="space-y-3">
            <input type="hidden" name="action" value="elementize_save_connection" />
            <input type="hidden" name="_wpnonce" value={config.nonces.saveConnection} />
            <div className="space-y-2">
              <Label htmlFor="elementize_public_api_origin">Address</Label>
              <Input
                id="elementize_public_api_origin"
                name="elementize_public_api_origin"
                type="url"
                defaultValue={config.environment.storedPublic}
                placeholder={config.environment.effectiveOrigin || 'https://your-secure-address.example'}
              />
            </div>
            <div className="flex flex-wrap gap-2">
              <Button type="submit">Save address</Button>
              {config.environment.storedPublic && (
                <Button type="submit" variant="outline" name="elementize_clear_public_origin" value="1">Use automatic address</Button>
              )}
            </div>
          </form>

          <Separator />

          <Accordion type="single" collapsible>
            <AccordionItem value="technical" className="border-0">
              <AccordionTrigger>Technical details</AccordionTrigger>
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
